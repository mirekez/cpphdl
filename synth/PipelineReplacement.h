#pragma once
#include "cpphdl_graph.h"

namespace cpphdl::synth {
inline bool pipelineInput(const std::string& name) {
    return name == "reset" || name == "command_valid_in" || name == "response_ready_in" ||
           name == "operation_in" || name == "index_in" || name == "value_in";
}

// Keep the original scheduled graph until timing has selected the latency.
// Linking substitutes only complete regions; it never matches arithmetic gates.
inline void linkPipelineReplacements(graph::Graph& g) {
    using namespace graph;
    if (!g.replacements.empty()) return;
    const auto previousScope = g.currentScope;
    std::set<std::string> replacedScopes;
    if (g.clockContract != ClockContract::RisingEdgeStep && !g.pipelines.empty()) {
        for (const auto& a : g.attributes) if (a.name == "pipeline_replacement")
            throw std::runtime_error("pipeline replacement requires the default rising clock");
    }
    for (const auto& p : g.pipelines) {
        std::string module, body;
        unsigned minimum = 1;
        for (const auto& a : g.attributes) if (a.scope == p.scope) {
            if (a.name == "pipeline_replacement") module = a.value;
            if (a.name == "pipeline_replacement_body") body = a.value;
            if (a.name == "pipeline_replacement_min_latency") minimum = std::stoul(a.value);
        }
        if (module.empty()) continue;
        if (body.empty() || !minimum || p.latency < minimum)
            throw std::runtime_error("replacement LATENCY is below its minimum: " + module);
        PipelineReplacement r{p.scope,module,body,p.latency,p.pins};
        std::set<Value> owned;
        for (auto q : p.registers) owned.insert(g.resolved(q));
        g.states.erase(std::remove_if(g.states.begin(),g.states.end(),[&](const State& s) {
            return owned.count(g.resolved(s.bits));
        }),g.states.end());
        for (auto& [name,bits] : r.pins) bits = g.resolved(bits);
        for (auto& [name,bits] : r.pins) if (!pipelineInput(name)) {
            auto previous = bits;
            g.currentScope = p.scope + ".replacement";
            bits = g.wire(bits.size(),p.scope + ".replacement." + name,"state");
            // Constant output bits can already have propagated into callers.
            // Leave those constants intact; never alias the global 0/1 bits.
            for (size_t i=0; i<previous.size(); ++i)
                if (previous[i] > 1) g.connect({previous[i]},{bits[i]});
        }
        g.replacements.push_back(std::move(r));
        replacedScopes.insert(p.scope);
    }
    g.currentScope = previousScope;
    g.pipelines.erase(std::remove_if(g.pipelines.begin(),g.pipelines.end(),[&](const StreamPipeline& p) {
        return replacedScopes.count(p.scope);
    }),g.pipelines.end());
}
}
