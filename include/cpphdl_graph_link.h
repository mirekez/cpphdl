#pragma once
#include "cpphdl_graph.h"

namespace cpphdl::graph {

// Convert a synchronous module's clock contract to explicit event equations
// before flattening it into a multi-clock host graph. This does not schedule
// threads or introduce a simulation interface around the module.
inline void explicitClockEvents(Graph& graph, const std::string& defaultClock = "clk") {
    if (graph.clockContract == ClockContract::ExplicitEvents) return;
    if (graph.clockContract == ClockContract::RisingEdgeStep) {
        if (!graph.clocks.empty()) throw std::runtime_error("implicit graph has named clocks");
        graph.clocks.push_back({defaultClock, 0});
        for (auto& s : graph.states) s.clock = 0;
        for (auto& a : graph.memoryAccesses) a.clock = 0;
        for (auto& w : graph.memoryWrites) w.clock = 0;
    }
    for (const auto& s : graph.states)
        if (!s.reset.empty()) throw std::runtime_error("clock linking requires synchronous-reset modules");
    std::vector<std::pair<Value, Value>> edges;
    for (const auto& clock : graph.clocks) {
        Value level;
        for (const auto& p : graph.ports) if (p.input && p.name == clock.name) level = p.bits;
        if (level.empty()) {
            level = graph.wire(1, clock.name, "input");
            graph.ports.push_back({clock.name, level, true});
        }
        if (level.size() != 1) throw std::runtime_error("clock linking requires scalar clocks");
        auto previous = graph.add("state", 1, constant(0, 1), {}, {}, "event history");
        auto rising = graph.binary("and", level, graph.binary("xor", previous, {1}, 1), 1);
        auto falling = graph.binary("and", previous, graph.binary("xor", level, {1}, 1), 1);
        edges.push_back({rising, falling});
        graph.states.push_back({previous, level, {1}, -1, false, {}, {}});
    }
    auto qualify = [&](Value enable, int clock, bool falling) {
        if (clock < 0 || size_t(clock) >= edges.size()) throw std::runtime_error("invalid linked clock index");
        return graph.binary("and", enable.empty() ? Value{1} : enable,
                            falling ? edges[clock].second : edges[clock].first, 1);
    };
    for (auto& s : graph.states) if (s.clock >= 0) {
        s.trigger = qualify(s.trigger, s.clock, s.falling); s.clock = -1; s.falling = false;
    }
    for (auto& a : graph.memoryAccesses) {
        if (a.transaction) a.enabled = qualify(a.enabled, a.clock, a.falling);
        a.clock = -1; a.falling = false;
    }
    for (auto& w : graph.memoryWrites) {
        w.enabled = qualify(w.enabled, w.clock, w.falling); w.clock = -1; w.falling = false;
    }
    graph.clocks.clear();
    graph.clockContract = ClockContract::ExplicitEvents;
}

// Bind all inputs and any connected outputs, then erase hierarchy before
// native partitioning. The child's graph is copied; no evaluator is invoked.
inline void linkGraphInstance(Graph& host, const Graph& child,
                              const std::map<std::string, Value>& pins,
                              const std::string& instance) {
    if (host.clockContract != ClockContract::ExplicitEvents ||
        child.clockContract != ClockContract::ExplicitEvents ||
        !host.clocks.empty() || !child.clocks.empty())
        throw std::runtime_error("graph linking requires explicit clock events");
    // Streaming templates are scheduler metadata, not executable state. This
    // simulation link preserves the already scheduled equations and registers;
    // it deliberately does not make those templates available for re-retiming.
    for (const auto& p : child.ports) {
        auto found = pins.find(p.name);
        if (found == pins.end()) {
            if (p.input) throw std::runtime_error("missing linked input: " + p.name);
        } else if (found->second.size() != p.bits.size())
            throw std::runtime_error("linked port width differs: " + p.name);
    }
    for (const auto& [name, bits] : pins)
        if (std::none_of(child.ports.begin(), child.ports.end(), [&](const Port& p) { return p.name == name; }))
            throw std::runtime_error("unknown linked port: " + name);
    const auto base = host.nodes.size(), memoryBase = host.memories.size();
    auto bit = [&](Bit b) { return b < 2 ? b : (Graph::owner(b) + base + 1)*64 + 2 + Graph::lane(b); };
    auto value = [&](Value v) { for (auto& b : v) b = bit(b); return v; };
    for (const auto& original : child.nodes) {
        auto n = original;
        n.left = value(n.left); n.right = value(n.right); n.select = value(n.select);
        if (n.op == "input") n.op = "wire";
        if (n.op == "memory_read") {
            auto index = number(original.right);
            if (!index || *index >= child.memories.size()) throw std::runtime_error("invalid linked memory index");
            n.right = constant(memoryBase + *index, original.right.size());
        }
        n.scope = instance + "/" + n.scope;
        host.nodes.push_back(std::move(n));
    }
    for (auto [target, source] : child.aliases) host.aliases.emplace(bit(target), bit(source));
    for (auto s : child.states) {
        s.bits = value(s.bits); s.next = value(s.next); s.trigger = value(s.trigger);
        s.reset = value(s.reset); s.resetValue = value(s.resetValue);
        host.states.push_back(std::move(s));
    }
    for (auto m : child.memories) { m.name = instance + "/" + m.name; host.memories.push_back(std::move(m)); }
    for (auto a : child.memoryAccesses) {
        a.memory += memoryBase; a.address = value(a.address); a.enabled = value(a.enabled);
        host.memoryAccesses.push_back(std::move(a));
    }
    for (auto w : child.memoryWrites) {
        w.memory += memoryBase; w.address = value(w.address); w.data = value(w.data); w.enabled = value(w.enabled);
        host.memoryWrites.push_back(std::move(w));
    }
    for (auto a : child.attributes) { a.scope = instance + "/" + a.scope; host.attributes.push_back(std::move(a)); }
    for (const auto& p : child.ports) if (auto found = pins.find(p.name); found != pins.end()) {
        if (p.input) host.connect(value(p.bits), found->second);
        else host.connect(found->second, value(p.bits));
    }
}
}
