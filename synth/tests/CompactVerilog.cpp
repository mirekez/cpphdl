#ifdef VERILATOR
#include "VCompactMux.h"
#include <cstdio>
#include <cstdint>
int main() {
    VCompactMux dut;
    uint32_t held[3] = {};
    for (unsigned tick = 0; tick < 4096; ++tick) {
        bool reset = tick == 0 || tick % 137 == 0;
        bool enable = tick % 7 != 0, first = tick & 1, second = tick & 2;
        uint32_t a = tick * 0x1234567u, b = tick * 0xdeadbeefu;
        uint32_t selected = second ? (first ? a : b) : b;
        dut.clk = 0; dut.reset = reset; dut.enable = enable;
        dut.first = first; dut.second = second; dut.a = a; dut.b = b; dut.eval();
        if (dut.combination != selected) return 1;
        if (reset) for (auto& word : held) word = 0;
        else if (enable) {
            held[2] = held[1]; held[1] = held[0]; held[0] = selected;
        }
        dut.clk = 1; dut.eval(); dut.clk = 0; dut.eval();
        if (dut.result != held[2]) return 2;
    }
    std::puts("Compact mux Verilog: dependent muxes, vector slices, stalls and asynchronous reset passed");
}
#else
#include "Mapping.h"
#include "Verilog.h"
#include <cstdio>
#include <fstream>
using namespace cpphdl::graph;
int main(int argc, char** argv) {
    if (argc != 2) return 2;
    Graph graph; graph.clockContract = ClockContract::RisingEdgeStep;
    auto a = graph.wire(32, "a", "input"), b = graph.wire(32, "b", "input");
    auto first = graph.wire(1, "first", "input"), second = graph.wire(1, "second", "input");
    auto reset = graph.wire(1, "reset", "input"), enable = graph.wire(1, "enable", "input");
    auto unused = graph.wire(17, "unused", "input");
    graph.ports = {{"a",a,true},{"b",b,true},{"first",first,true},
        {"second",second,true},{"reset",reset,true},{"enable",enable,true},{"unused",unused,true}};
    auto selected = graph.mux(second, graph.mux(first,a,b), b);
    graph.ports.push_back({"combination",selected,false});
    auto previous = selected;
    for (unsigned i = 0; i < 3; ++i) {
        auto state = graph.wire(32, "held" + std::to_string(i), "state");
        graph.states.push_back({state,graph.mux(enable,previous,state),{1},-1,false,reset,constant(0,32)});
        previous = state;
    }
    graph.ports.push_back({"result",previous,false});
    auto unusedState = graph.wire(32, "unused_state", "state");
    graph.states.push_back({unusedState,constant(0,32),{1}});
    auto mapped = cpphdl::synth::mapGates(graph);
    auto text = cpphdl::synth::verilogText(mapped,"CompactMux");
    unsigned scalarWires = 0;
    for (size_t pos = 0; (pos = text.find("wire [0:0] __cpphdl_node_",pos)) != std::string::npos; ++pos)
        ++scalarWires;
    if (text.find("wire [31:0] __cpphdl_node_bundle_") == std::string::npos ||
        scalarWires > 4 || text.size() > 12000) return 1;
    std::ofstream file(argv[1]); file << text;
    if (!file) return 1;
    std::printf("Compact mux netlist: %zu bytes\n",text.size());
}
#endif
