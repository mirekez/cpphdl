#pragma once
#include "cpphdl_graph.h"

namespace cpphdl::synth {
// Generic, technology-independent bit gates. Explicit keep boxes are opaque.
graph::Graph mapGates(graph::Graph graph);
// Expand selected operations without changing memory or register boundaries.
graph::Graph expandGates(graph::Graph graph, const std::set<size_t>& operations);
void writeGateReport(graph::Graph& graph, const std::string& path, const std::string& module);
}
