#include "timing.h"
#include "KeepBoxes.h"
#include "BlackBox.h"
#include <cmath>

namespace cpphdl::synth {
using namespace graph;
double cellDelay(const Graph& graph, const Node& node, const DelayModel& m) {
    auto levels = [](uint64_t n) { return std::ceil(std::log2(double(std::max<uint64_t>(2, n)))); };
    auto width = std::max(node.left.size(), node.right.size());
    if (node.op == "input" || node.op == "wire") return 0;
    if (node.op == "state") return m.clockToQ;
    if (node.op == "blackbox") return blackBoxDelay(node);
    if (node.op == "and" || node.op == "or") return m.gate;
    if (node.op == "xor") return 2 * m.gate;
    if (node.op == "mux") return m.mux;
    if (node.op == "any" || node.op == "all" || node.op == "parity") return levels(node.left.size()) * 2 * m.gate;
    if (node.op == "add" || node.op == "sub" || node.op == "lt" || node.op == "slt") return m.gate + width * m.carry;
    if (node.op == "eq") return 2 * m.gate + levels(width) * m.gate;
    if (node.op == "mul") return levels(width) * (m.gate + width * m.carry);
    if (node.op == "div" || node.op == "mod" || node.op == "sdiv" || node.op == "smod") return width * (m.gate + width * m.carry);
    if (node.op == "shl" || node.op == "shr" || node.op == "sar") return number(node.right) ? 0 : levels(width) * m.mux;
    if (node.op == "memory_read") {
        auto id = number(node.right);
        if (!id || *id >= graph.memories.size()) throw std::runtime_error("invalid timing memory read");
        return m.memoryBase + levels(graph.memories[*id].depth) * m.memoryLevel;
    }
    throw std::runtime_error("no delay estimate for operation: " + node.op);
}
template<class Endpoint>
static void timingEndpoints(Graph& graph, const DelayModel& model, const std::string& scope, Endpoint endpoint) {
    auto inside = [&](const std::string& name) { return scope.empty() || name == scope || name.find(scope + ".") == 0; };
    for (const auto& state : graph.states) {
        const auto& name = graph.nodes.at(Graph::owner(state.bits[0])).name;
        if (inside(name)) endpoint(state.next, model.setup, "register " + name);
    }
    for (const auto& port : graph.ports) if (!port.input) {
        bool include = scope.empty();
        for (auto bit : graph.resolved(port.bits)) if (bit > 1) {
            const auto& n = graph.nodes[Graph::owner(bit)]; include |= inside(n.scope) || inside(n.name);
        }
        if (include) endpoint(port.bits, 0, "output " + port.name);
    }
    for (const auto& write : graph.memoryWrites) {
        if (!inside(graph.memories.at(write.memory).name)) continue;
        endpoint(write.address, model.setup, "memory write address");
        endpoint(write.data, model.setup, "memory write data");
        endpoint(write.enabled, model.setup, "memory write enable");
    }
}
TimingReport estimateTiming(Graph& graph, const DelayModel& model, const std::string& scope) {
    for (double delay : {model.gate, model.mux, model.carry, model.clockToQ, model.setup, model.memoryBase, model.memoryLevel})
        if (!std::isfinite(delay) || delay < 0) throw std::runtime_error("cell delays must be finite and nonnegative");
    graph.optimize();
    auto order = graph.dependencyOrder();
    // A one-clock function has an estimated, not user-invented, delay. Its
    // external operands start at time zero; helper calls belong to this scope.
    {
        KeepBoxes groups(graph);
        for (auto& attr : graph.attributes) if (attr.name == "one_clock") {
            std::map<size_t, double> arrival;
            double delay = 0;
            for (auto n : order) if (groups.owner.count(n) && groups.boxes[groups.owner.at(n)].scope == attr.scope) {
                double incoming = 0;
                for (const auto* v : {&graph.nodes[n].left, &graph.nodes[n].right, &graph.nodes[n].select})
                    for (auto bit : graph.resolved(*v)) if (bit > 1 && arrival.count(Graph::owner(bit)))
                        incoming = std::max(incoming, arrival.at(Graph::owner(bit)));
                arrival[n] = incoming + cellDelay(graph, graph.nodes[n], model);
                delay = std::max(delay, arrival[n]);
            }
            std::ostringstream text; text << std::setprecision(17) << delay;
            attr.value = text.str();
        }
    }
    KeepBoxes boxes(graph);
    TimingReport report;
    report.arrival.resize(graph.nodes.size());
    // Node and box identities are dense indices. Timing is repeated while
    // scheduling large source pipelines; avoid per-node tree allocation and
    // logarithmic membership checks on every traversal.
    // 0 = unseen, 1 = active, 2 = complete (the active mark still detects cycles).
    std::vector<unsigned char> nodeStatus(graph.nodes.size()), boxStatus(boxes.boxes.size());
    std::function<double(size_t)> nodeArrival;
    auto arrival = [&](const Value& value) {
        double result = 0;
        for (auto bit : graph.resolved(value)) if (bit > 1) result = std::max(result, nodeArrival(Graph::owner(bit)));
        return result;
    };
    nodeArrival = [&](size_t index) {
        if (boxes.owner.count(index)) {
            auto b = boxes.owner.at(index);
            if (boxStatus[b] != 2) {
                if (boxStatus[b] == 1) throw std::runtime_error("combinational path leaves and re-enters keep box");
                boxStatus[b] = 1;
                double time = arrival(boxes.boxes[b].inputs) + boxes.boxes[b].delay;
                for (auto n : boxes.boxes[b].nodes) report.arrival[n] = time;
                boxStatus[b] = 2;
            }
            return report.arrival[index];
        }
        if (nodeStatus[index] == 2) return report.arrival[index];
        if (nodeStatus[index] == 1) throw std::runtime_error("cyclic timing graph");
        nodeStatus[index] = 1;
        const auto& node = graph.nodes[index];
        double incoming = node.op == "state" ? 0 : std::max({arrival(node.left), arrival(node.right), arrival(node.select)});
        report.arrival[index] = incoming + cellDelay(graph, node, model);
        nodeStatus[index] = 2;
        return report.arrival[index];
    };
    for (auto index : order) nodeArrival(index);
    auto endpoint = [&](const Value& value, double extra, const std::string& name) {
        double delay = arrival(value) + extra;
        if (delay > report.worst) { report.worst = delay; report.endpoint = name; }
    };
    timingEndpoints(graph,model,scope,endpoint);
    return report;
}
ScopedTimingReport estimateScopedTiming(Graph& graph, const DelayModel& model, const std::string& scope,
    const std::map<std::string,Value>& boundaryPins) {
    auto full=[&]() {
        const auto timing=estimateTiming(graph,model,scope);
        ScopedTimingReport result;result.worst=timing.worst;result.endpoint=timing.endpoint;
        for(const auto& [name,bits]:boundaryPins)for(auto bit:graph.resolved(bits))if(bit>1) {
            auto n=Graph::owner(bit);result.arrival[n]=timing.arrival.at(n);
        }
        return result;
    };
    // Keep-box timing can depend on inputs outside the selected node cone;
    // one-clock annotations also require recomputing their aggregate delays.
    if(scope.empty() || std::any_of(graph.attributes.begin(),graph.attributes.end(),
        [](const ScopeAttribute& a){return a.name=="keep_box" || a.name=="one_clock";}))return full();
    for(double delay:{model.gate,model.mux,model.carry,model.clockToQ,model.setup,model.memoryBase,model.memoryLevel})
        if(!std::isfinite(delay) || delay<0)throw std::runtime_error("cell delays must be finite and nonnegative");
    struct NeedsPreparation {};
    struct Visit {unsigned char status=0;double arrival=0;};
    std::unordered_map<size_t,Visit> visits;
    ScopedTimingReport result;
    struct Frame {size_t index;unsigned operand=0;size_t offset=0;double incoming=0;};
    std::vector<Frame> stack;
    auto nodeArrival=[&](size_t index) {
        if(visits[index].status==2)return visits[index].arrival;
        visits[index].status=1;stack.push_back({index});
        // Whole linked designs can have deep control cones. Heap-backed DFS
        // avoids depending on the process stack size, as dependencyOrder does.
        while(!stack.empty()) {
            auto& frame=stack.back();
            const auto& node=graph.nodes.at(frame.index);
            if(node.op=="wire")throw NeedsPreparation{};
            if(node.op=="state" || frame.operand==3) {
                auto& visit=visits[frame.index];
                visit.arrival=frame.incoming+cellDelay(graph,node,model);visit.status=2;
                stack.pop_back();continue;
            }
            const auto& value=frame.operand==0?node.left:frame.operand==1?node.right:node.select;
            if(frame.offset==value.size()){++frame.operand;frame.offset=0;continue;}
            const auto bit=graph.resolve(value[frame.offset]);
            if(bit<=1){++frame.offset;continue;}
            const auto owner=Graph::owner(bit);
            const auto& child=visits[owner];
            if(child.status==2){frame.incoming=std::max(frame.incoming,child.arrival);++frame.offset;continue;}
            // Word-level false cycles may need bit refinement; full preparation
            // handles those and diagnoses true cycles/undriven bits.
            if(child.status==1)throw NeedsPreparation{};
            visits[owner].status=1;stack.push_back({owner});
        }
        return visits.at(index).arrival;
    };
    auto arrival=[&](const Value& value) {
        double time=0;
        for(auto bit:graph.resolved(value))if(bit>1)time=std::max(time,nodeArrival(Graph::owner(bit)));
        return time;
    };
    try {
        timingEndpoints(graph,model,scope,[&](const Value& value,double extra,const std::string& name) {
            const auto delay=arrival(value)+extra;
            if(delay>result.worst){result.worst=delay;result.endpoint=name;}
        });
        for(const auto& [name,bits]:boundaryPins)arrival(bits);
    } catch(const NeedsPreparation&) {return full();}
    for(const auto& [index,visit]:visits)result.arrival[index]=visit.arrival;
    return result;
}

}
