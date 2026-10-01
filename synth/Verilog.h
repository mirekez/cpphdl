#pragma once

#include "cpphdl_graph.h"

namespace cpphdl::synth {
// Emit a synchronous operation netlist. Technology mapping is a separate pass.
void emitVerilog(graph::Graph& graph, const std::string& path, const std::string& module);
}
