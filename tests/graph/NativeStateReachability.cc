#include "CppGraphReachability.h"
#include <cassert>
#include <iostream>

using namespace cpphdl::graph;

static Graph machine(bool forceInvalid = false, bool expose = false, bool badReset = false,
                     bool initialMissing = false) {
    Graph g;
    auto q = g.wire(3, "state", "state");
    auto go = g.wire(1, "advance", "input");
    auto rst = g.wire(1, "reset", "input");
    auto hole = g.wire(3, "undefined_fallback");
    auto next = hole;
    for (unsigned value = initialMissing ? 1 : 0; value < 5; ++value) {
        auto transfer = g.mux(go, constant((value + 1) % 5, 3), constant(value, 3));
        next = g.mux(g.binary("eq", q, constant(value, 3), 1), transfer, next);
    }
    if (forceInvalid) next = g.mux(go, constant(7, 3), next);
    next = g.mux(rst, constant(0, 3), next);
    g.states.push_back({q, next, {1}});
    if (badReset) { g.states[0].reset = rst; g.states[0].resetValue = constant(7, 3); }
    g.ports = {{"advance", go, true}, {"reset", rst, true}, {"state", q, false}};
    if (expose) g.ports.push_back({"exposed", hole, false});
    return g;
}

static Value fallback(const Graph& g) {
    for (size_t id = 0; id < g.nodes.size(); ++id)
        if (g.nodes[id].name == "undefined_fallback:0")
            return {(id + 1) * 64 + 2, (id + 1) * 64 + 3, (id + 1) * 64 + 4};
    throw std::runtime_error("missing test fallback");
}

int main() {
    auto good = machine();
    assert(cpphdl::cpp_graph::NativeStateReachability(good).run() == 1);
    good.optimize(); good.compact();
    // Correlated bit aliases for the 3 -> {3,4} transition must not admit 7.
    assert(good.states.size() == 1);
    for (auto bad : {machine(true), machine(false, true), machine(false, false, true),
                     machine(false, false, false, true)}) {
        assert(cpphdl::cpp_graph::NativeStateReachability(bad).run() == 0);
        bool rejected = false;
        try { bad.optimize(); bad.compact(); } catch (const std::runtime_error&) { rejected = true; }
        assert(rejected);
    }
    // A second next-state cone exposing the fallback is also observable.
    auto cross = machine();
    auto data = cross.wire(3, "data", "state");
    cross.states.push_back({data, fallback(cross), {1}});
    cross.ports.push_back({"data", data, false});
    assert(cpphdl::cpp_graph::NativeStateReachability(cross).run() == 0);
    bool rejected = false;
    try { cross.optimize(); cross.compact(); } catch (const std::runtime_error&) { rejected = true; }
    assert(rejected);
    auto memory = machine();
    memory.memories.push_back({"ram", 3, 1});
    memory.memoryWrites.push_back({0, {0}, fallback(memory), {1}});
    assert(cpphdl::cpp_graph::NativeStateReachability(memory).run() == 0);
    auto effect = machine();
    effect.add("assert_failure", 1, fallback(effect), {}, {1});
    assert(cpphdl::cpp_graph::NativeStateReachability(effect).run() == 0);
    std::cout << "native state proof: closed FSM accepted; reachable, observed and reset-invalid fallbacks rejected\n";
}
