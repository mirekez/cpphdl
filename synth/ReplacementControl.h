#pragma once
#include "cpphdl_graph.h"

namespace cpphdl::synth {

// Find scalar launch registers shared by many hard-block handshakes. Count
// replacement instances, not graph edges: one ready pin can drive a wide
// register bank inside the replacement, which is opaque to graph retiming.
// Stop at registers and non-scalar operations; never walk through a hard
// block, RAM, or a payload bus to infer a control-register assignment.
inline std::map<size_t, size_t> replacementControlLoads(graph::Graph& g) {
    using namespace graph;
    if (g.replacements.empty()) return {};
    std::set<size_t> registers;
    for (const auto& s : g.states) {
        if (!s.reset.empty() || s.clock > 0 || s.falling) continue;
        for (auto bit : g.resolved(s.bits)) if (bit > 1) {
            const auto owner = Graph::owner(bit);
            if (g.nodes[owner].op == "state" && g.nodes[owner].width == 1)
                registers.insert(owner);
        }
    }
    std::map<Bit, std::set<size_t>> roots;
    std::map<size_t, size_t> loads;
    for (const auto& r : g.replacements) {
        std::set<size_t> consumers;
        for (const char* name : {"command_valid_in", "response_ready_in"}) {
            const auto pin = r.pins.find(name);
            if (pin == r.pins.end() || pin->second.size() != 1) continue;
            const auto bit = g.resolve(pin->second.front());
            auto [found, inserted] = roots.try_emplace(bit);
            if (inserted) {
                std::vector<Bit> pending{bit};
                std::set<size_t> visited;
                while (!pending.empty()) {
                    const auto b = g.resolve(pending.back()); pending.pop_back();
                    if (b < 2) continue;
                    const auto owner = Graph::owner(b);
                    if (!visited.insert(owner).second) continue;
                    const auto& node = g.nodes[owner];
                    if (node.width != 1) continue;
                    if (node.op == "state") {
                        if (registers.count(owner)) found->second.insert(owner);
                        continue;
                    }
                    if (node.op != "and" && node.op != "or" && node.op != "xor" &&
                        node.op != "not" && node.op != "mux") continue;
                    for (const auto* operand : {&node.left, &node.right, &node.select}) {
                        if (operand->size() > 1) continue;
                        pending.insert(pending.end(), operand->begin(), operand->end());
                    }
                }
            }
            consumers.insert(found->second.begin(), found->second.end());
        }
        for (auto owner : consumers) ++loads[owner];
    }
    return loads;
}
}
