#pragma once
#include "cpphdl_graph.h"

namespace cpphdl::synth {
// Persist only the reachable calculation and its callees. Each memory cut
// starts from a copy of its source region; serializing that whole copy keeps
// unrelated operations and every nested method body once per cut, which makes
// a long source function's compiler memory grow far beyond its live hardware.
inline void compactPipelineLogic(graph::Graph& logic) {
    logic.optimize();
    std::set<std::string> callees;
    for(auto index:logic.dependencyOrder()) {
        const auto& node=logic.nodes[index];
        if(node.op=="atomic_call" || node.op=="pipeline_call")callees.insert(node.name);
    }
    logic.attributes.erase(std::remove_if(logic.attributes.begin(),logic.attributes.end(),
        [&](const graph::ScopeAttribute& a){return a.name=="atomic_method" && !callees.count(a.scope);}),logic.attributes.end());
    logic.compact();
}
struct AtomicMethod {
    std::string module, body, logic;
    unsigned minimumLatency = 1, argumentBits = 32, resultBits = 32;
    std::string serialize() const {
        std::ostringstream out;
        out << std::quoted(module) << ' ' << std::quoted(body) << ' ' << std::quoted(logic)
            << ' ' << minimumLatency << ' ' << argumentBits << ' ' << resultBits;
        return out.str();
    }
    static AtomicMethod parse(const std::string& text) {
        AtomicMethod method; std::istringstream in(text);
        if (!(in >> std::quoted(method.module) >> std::quoted(method.body) >> std::quoted(method.logic)
              >> method.minimumLatency >> method.argumentBits >> method.resultBits) ||
            !method.minimumLatency || method.minimumLatency > 64 || !method.argumentBits || !method.resultBits)
            throw std::runtime_error("invalid atomic method metadata");
        return method;
    }
};
inline bool nestedAtomic(const graph::Graph& graph, const std::string& scope) {
    for (const auto& a : graph.attributes) if (a.scope == scope && a.name == "atomic_parent") return true;
    return false;
}
}
