#include "retiming.h"
#include <cstdio>

using namespace cpphdl::graph;
using namespace cpphdl::synth;

#define CHECK(condition) do { if (!(condition)) { std::fprintf(stderr, "line %d: %s\n", __LINE__, #condition); return 1; } } while (false)

static Graph chain() {
    Graph g; g.clockContract = ClockContract::RisingEdgeStep;
    g.currentScope = "top.stage";
    auto input = g.wire(8, "data", "input");
    auto state = g.wire(8, "top.stage.result", "state");
    auto data = input;
    for (unsigned i = 0; i < 4; ++i) {
        data = g.binary("add", data, constant(13, 8), 8);
        data = g.binary("xor", data, constant(0x59, 8), 8);
    }
    g.states.push_back({state, data, {1}});
    g.ports = {{"data", input, true}, {"result", state, false}};
    return g;
}
static bool sameCycles(Graph before, Graph after) {
    auto initial = [](const Graph& g) {
        std::vector<uint64_t> values;
        for (const auto& s : g.states) values.push_back(number(g.nodes[Graph::owner(s.bits[0])].left).value_or(0));
        return values;
    };
    auto sample = [](Graph g, const std::vector<uint64_t>& values, unsigned input) {
        for (const auto& port : g.ports) if (port.input) g.connect(port.bits, constant(input, port.bits.size()));
        for (size_t i = 0; i < g.states.size(); ++i) g.connect(g.states[i].bits, constant(values[i], g.states[i].bits.size()));
        g.optimize();
        std::vector<uint64_t> next;
        for (const auto& state : g.states) next.push_back(number(g.resolved(state.next)).value());
        return std::make_pair(number(g.resolved(g.ports.back().bits)).value(), next);
    };
    auto oldState = initial(before), newState = initial(after);
    for (unsigned cycle = 0; cycle < 256; ++cycle) {
        auto a = sample(before, oldState, cycle * 37);
        auto b = sample(after, newState, cycle * 37);
        if (a.first != b.first) return false;
        oldState = a.second; newState = b.second;
    }
    return true;
}
int main() {
    auto rejects = [](Graph g, RetimingRule rule, const char* message) {
        auto originalStates = g.states.size(), originalNodes = g.nodes.size();
        try { retime(g, rule); }
        catch (const std::exception& error) {
            if (std::string(error.what()).find(message) != std::string::npos &&
                g.states.size() == originalStates && g.nodes.size() == originalNodes) return true;
            std::fprintf(stderr, "unexpected rejection: %s\n", error.what());
        }
        return false;
    };
    auto fit = RetimingRule{"fit_pipeline_retiming", 0.9, "top.stage"};
    auto g = chain();
    auto baseline = estimateTiming(g).worst;
    auto report = retime(g, fit);
    CHECK(report.met && report.addedLatency && report.after < baseline);
    auto slow = DelayModel{}; slow.carry *= 2;
    auto original = chain();
    CHECK(estimateTiming(original, slow).worst > baseline);
    CHECK(rejects(chain(), {"fit_pipeline_retiming", 0.2, ""}, "indivisible cell"));
    CHECK(rejects(chain(), {"unknown", 1, ""}, "unknown retiming"));
    CHECK(rejects(chain(), {"fit_pipeline_retiming", 1, "missing"}, "no registers"));
    g = chain();
    auto reset = g.wire(1, "reset", "input");
    g.ports.insert(g.ports.begin(), {"work_reset", reset, true});
    auto unusedReset = g;
    CHECK(retime(unusedReset, fit).met);
    g.states[0].next = g.mux(reset, g.ports[1].bits, g.states[0].next);
    CHECK(rejects(g, fit, "constant synchronous reset"));
    g = chain(); g.states[0].next = g.binary("add", g.states[0].bits, constant(1, 8), 8);
    CHECK(rejects(g, fit, "feedback"));
    g = chain(); g.memories.push_back({"top.ram", 8, 4});
    g.memoryWrites.push_back({0, constant(0, 2), g.states[0].bits, {1}});
    CHECK(rejects(g, fit, "memory write"));
    g = chain();
    auto outside = g.wire(8, "top.other", "state");
    g.states.push_back({outside, g.states[0].bits, {1}});
    CHECK(rejects(g, fit, "unselected register"));
    g = chain(); g.clockContract = ClockContract::NamedEdges; g.clocks = {{"a", 100}, {"b", 50}};
    g.states[0].clock = 0;
    auto other = g.wire(8, "top.stage.other", "state");
    g.states.push_back({other, constant(0, 8), {1}, 1});
    CHECK(rejects(g, fit, "one clock"));
    g = chain();
    auto keep = retime(g, {"keep_behaviour_retiming", 0.9, ""});
    CHECK(keep.moved && !keep.addedLatency && keep.after < keep.before);
    CHECK(sameCycles(chain(), g));
    auto initialized = chain();
    initialized.nodes[Graph::owner(initialized.states[0].bits[0])].left = constant(0x83, 8);
    g = initialized;
    keep = retime(g, {"keep_behaviour_retiming", 0.9, ""});
    CHECK(keep.moved && sameCycles(initialized, g));
    g = chain(); g.memories.push_back({"ram", 8, 1024});
    auto small = Node{"memory_read", 8, constant(0, 10), constant(0, 64), constant(0, 64), "", ""};
    double deep = cellDelay(g, small, {}); g.memories[0].depth = 4;
    CHECK(cellDelay(g, small, {}) < deep);
    Graph fields;
    auto data = fields.wire(4, "data", "input");
    auto a = fields.wire(2, "a"), b = fields.wire(2, "b");
    auto aValue = fields.add("and", 2, {data[0], b[0]}, {data[1], 1});
    auto bValue = fields.add("and", 2, {data[2], a[0]}, {data[3], 1});
    fields.connect(a, aValue); fields.connect(b, bValue);
    fields.ports = {{"data", data, true}, {"result", aValue, false}};
    auto oldSize = fields.nodes.size();
    auto fieldTiming = estimateTiming(fields);
    CHECK(fields.nodes.size() > oldSize && fieldTiming.arrival.size() == fields.nodes.size());
    CHECK(fieldTiming.worst > 0);
    auto boxed = chain();
    for (auto& n : boxed.nodes) if (n.op != "state" && n.op != "input") n.scope = "top.stage.math";
    boxed.attributes.push_back({"top.stage.math", "keep_box", "0.7"});
    CHECK(std::abs(estimateTiming(boxed).worst - 0.75) < 1e-9);
    auto unchanged = boxed;
    auto boxKeep = retime(unchanged, {"keep_behaviour_retiming", 0.2, ""});
    CHECK(!boxKeep.moved && !boxKeep.met && sameCycles(boxed, unchanged));
    CHECK(retime(unchanged, fit).met);
    boxed.attributes[0].value = "1.0";
    CHECK(rejects(boxed, fit, "indivisible keep box"));
    boxed.attributes[0].value = "nan";
    CHECK(rejects(boxed, fit, "positive delay"));
    boxed.attributes[0].value = "0.7";
    boxed.attributes.push_back({"top.stage.math.inner", "keep_box", "0.1"});
    CHECK(rejects(boxed, fit, "overlapping keep boxes"));
    boxed.attributes = {{"top.stage", "keep_box", "0.7"}};
    CHECK(rejects(boxed, fit, "contains register"));
    auto pair = chain();
    for (size_t i = 2; i < pair.nodes.size(); ++i) pair.nodes[i].scope = i < 6 ? "top.stage.first" : "top.stage.second";
    pair.attributes = {{"top.stage.first", "keep_box", "0.3"}, {"top.stage.second", "keep_box", "0.3"}};
    auto pairFit = retime(pair, {"fit_pipeline_retiming", 0.5, "top.stage"});
    CHECK(pairFit.met && pairFit.addedLatency == 1);
    std::puts("timing and retiming: estimates, scope, feedback, domains, RAM barriers and atomic failure passed");
}
