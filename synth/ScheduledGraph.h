#pragma once
#include "../include/cpphdl_graph.h"
#include "../hls/SharedBlocks.h"

namespace cpphdl::synth {
// Source scheduler handoff, before SV function sharing/emission. Expressions
// use the scheduler's explicitly sized integer language, not a parsed module.
struct ScheduledDesign {
    std::vector<hls::ScheduledBlock> blocks;
    std::map<std::string, hls::BlockSymbol> symbols;
    std::map<std::string, graph::Value> constants;
    std::set<std::string> clocked;
    int resetEntry = 0, commandEntry = 0;
    unsigned addressBits = 16, memoryBytes = 32, heapBase = 32, heapBytes = 64, portBytes = 8;
    bool sharedMemory = false, blockRam = false;
};
std::map<std::string, graph::Value> exportScheduledGraph(graph::Graph&, const ScheduledDesign&, const std::string& scope);
// One transition: state reads and next values are explicit graph boundaries.
// No waits or memory transactions; reset values come from the C++ initializer.
std::map<std::string, graph::Value> exportCombinationalCommand(graph::Graph&, const ScheduledDesign&, const std::string& scope);
}
