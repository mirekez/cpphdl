#pragma once
#include "cpphdl_graph.h"
#include <cmath>

namespace cpphdl::synth {
inline bool boxScope(const std::string& name, const std::string& scope) {
    return name == scope || name.find(scope + ".") == 0 || name.find(scope + ":") == 0;
}
struct KeepBox {
    std::string scope, module;
    double delay = 0;
    std::vector<size_t> nodes;
    graph::Value inputs, outputs;
};
// Boundaries are derived from live graph connectivity, not from C++ field order.
// The same region contract is used by timing, scheduling and Verilog emission.
struct KeepBoxes {
    std::vector<KeepBox> boxes;
    std::map<size_t, size_t> owner;
    explicit KeepBoxes(graph::Graph& g) {
        for (const auto& attr : g.attributes) if (attr.name == "keep_box" || attr.name == "one_clock") {
            size_t used = 0;
            double delay = std::stod(attr.value, &used);
            if (used != attr.value.size() || !std::isfinite(delay) || delay < 0 ||
                (delay == 0 && attr.name == "keep_box") || attr.scope.empty())
                throw std::runtime_error("keep box requires a positive delay in ns: " + attr.scope);
            for (const auto& box : boxes) if (boxScope(box.scope, attr.scope) || boxScope(attr.scope, box.scope))
                throw std::runtime_error("overlapping keep boxes: " + attr.scope);
            boxes.push_back({attr.scope, "__cpphdl_keep_box_" + std::to_string(boxes.size()), delay});
        }
        if (boxes.empty()) return;
        for (const auto& state : g.states) for (const auto& box : boxes)
            if (boxScope(g.nodes.at(graph::Graph::owner(state.bits.at(0))).name, box.scope))
                throw std::runtime_error("keep box must be combinational (contains register): " + box.scope);
        for (const auto& mem : g.memories) for (const auto& box : boxes)
            if (boxScope(mem.name, box.scope)) throw std::runtime_error("keep box cannot contain memory: " + box.scope);
        auto order = g.dependencyOrder();
        for (auto n : order) for (size_t b = 0; b < boxes.size(); ++b) if (boxScope(g.nodes[n].scope, boxes[b].scope)) {
            const auto& node = g.nodes[n];
            if (node.op == "input" || node.op == "wire" || node.op == "state") continue;
            if (node.op == "memory_read" || node.hostEffect())
                throw std::runtime_error("keep box requires pure combinational logic: " + boxes[b].scope);
            owner[n] = b; boxes[b].nodes.push_back(n);
        }
        std::vector<std::set<graph::Bit>> inputs(boxes.size()), outputs(boxes.size());
        auto edge = [&](const graph::Value& v, std::optional<size_t> consumer) {
            for (auto bit : g.resolved(v)) if (bit > 1) {
                auto producer = owner.find(graph::Graph::owner(bit));
                if (consumer && (producer == owner.end() || producer->second != *consumer)) inputs[*consumer].insert(bit);
                if (producer != owner.end() && (!consumer || producer->second != *consumer)) outputs[producer->second].insert(bit);
            }
        };
        for (auto n : order) {
            std::optional<size_t> b;
            if (owner.count(n)) b = owner.at(n);
            edge(g.nodes[n].left, b); edge(g.nodes[n].right, b); edge(g.nodes[n].select, b);
        }
        for (const auto& p : g.ports) if (!p.input) edge(p.bits, {});
        for (const auto& s : g.states) { edge(s.next, {}); edge(s.reset, {}); }
        for (const auto& w : g.memoryWrites) { edge(w.address, {}); edge(w.data, {}); edge(w.enabled, {}); }
        for (const auto& a : g.memoryAccesses) { edge(a.address, {}); edge(a.enabled, {}); }
        for (size_t b = 0; b < boxes.size(); ++b) {
            boxes[b].inputs.assign(inputs[b].begin(), inputs[b].end());
            boxes[b].outputs.assign(outputs[b].begin(), outputs[b].end());
        }
    }
};
}
