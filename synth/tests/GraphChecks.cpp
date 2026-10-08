#include "Verilog.h"
#include <cstdio>

using namespace cpphdl::graph;

static Graph simple() {
    Graph graph;
    graph.clockContract = ClockContract::RisingEdgeStep;
    auto input = graph.wire(8, "a", "input");
    graph.ports = {{"a", input, true}, {"result", graph.binary("add", input, constant(1, 8), 8), false}};
    return graph;
}

int main(int argc, char** argv) {
    if (argc != 2) return 2;
    auto rejected = [&](Graph graph, const std::string& message) {
        try { cpphdl::synth::emitVerilog(graph, argv[1], "Test"); }
        catch (const std::exception& error) {
            if (std::string(error.what()).find(message) != std::string::npos) return true;
            std::fprintf(stderr, "wrong diagnostic: %s\n", error.what());
        }
        return false;
    };
    auto graph = simple();
    cpphdl::synth::emitVerilog(graph, argv[1], "Test");
    Graph legacy;
    legacy.load("0\n0\n0\n0\n");
    if (legacy.clockContract != ClockContract::ExplicitEvents) return 1;
    Graph current;
    current.load("0\n0\n0\n0\n0\n0\n0\nclock_contract 1\n");
    if (current.clockContract != ClockContract::RisingEdgeStep) return 1;
    graph.clockContract = ClockContract::ExplicitEvents;
    if (!rejected(graph, "lifecycle")) return 1;
    graph = simple(); graph.memories.push_back({"ram", 8, 16});
    cpphdl::synth::emitVerilog(graph, argv[1], "Test");
    graph.memoryWrites.push_back({1, constant(0, 4), constant(0, 8), {1}});
    if (!rejected(graph, "invalid graph memory write")) return 1;
    graph = simple();
    auto romAddress=graph.ports[0].bits;
    graph.ports.push_back({"rom",graph.constantArray(constant(0x12345abc,36),romAddress,9),false});
    auto romText=cpphdl::synth::verilogText(graph,"Rom");
    if(romText.find("__cpphdl_memory_0[0] = 9'b010111100;")==std::string::npos ||
       romText.find("__cpphdl_memory_0[3] = 9'b000000010;")==std::string::npos) return 1;
    graph.memoryWrites.push_back({0,constant(0,8),constant(0,9),{1}});
    if(!rejected(graph,"write to graph ROM")) return 1;
    graph = simple(); graph.add("host_random", 32, {}, {}, {1});
    if (!rejected(graph, "host effects")) return 1;
    graph = simple(); graph.ports[0].name = "clk";
    if (!rejected(graph, "conflicting")) return 1;
    graph = simple();
    auto state = graph.wire(8, "state", "state");
    graph.states.push_back({state, constant(0, 8), {0}});
    if (!rejected(graph, "state/event")) return 1;
    graph = simple(); graph.clockContract = ClockContract::NamedEdges;
    graph.clocks = {{"main_clk", 100}, {"secondary_clk", 25}};
    state = graph.wire(8, "state", "state");
    graph.states.push_back({state, constant(0, 8), {1}, 2, false});
    if (!rejected(graph, "invalid state clock")) return 1;
    graph.states[0].clock = 0;
    graph.states.push_back({state, constant(1, 8), {1}, 1, false});
    if (!rejected(graph, "multiple clock/edge")) return 1;
    graph.states.pop_back();
    graph.clocks[1].name = "main_clk";
    if (!rejected(graph, "duplicate clock")) return 1;
    graph.clocks[1].name = "a";
    if (!rejected(graph, "conflicts with data port")) return 1;
    graph.clocks[1].name = "secondary_clk";
    graph.memories.push_back({"ram", 8, 4});
    graph.memoryWrites.push_back({0, constant(0, 2), constant(7, 8), {1}, 0, false});
    graph.memoryWrites.push_back({0, constant(1, 2), constant(9, 8), {1}, 1, false});
    if (!rejected(graph, "multiple clock/edge writers")) return 1;
    graph.memoryWrites.pop_back();
    graph.memoryWrites[0].clock = 2;
    if (!rejected(graph, "invalid memory clock")) return 1;
    graph.memoryWrites[0].clock = 0;
    graph.states[0].reset = graph.ports[0].bits;
    graph.states[0].resetValue = constant(0, 8);
    if (!rejected(graph, "constant full-width")) return 1;
    graph.states[0].reset = graph.wire(1, "reset", "input");
    graph.ports.push_back({"reset", graph.states[0].reset, true});
    graph.states[0].resetValue = state;
    if (!rejected(graph, "constant full-width")) return 1;
    graph.states[0].resetValue = constant(0xa5, 8);
    cpphdl::synth::emitVerilog(graph, argv[1], "Test");
    graph = simple(); graph.ports[1].bits = graph.wire(8, "missing");
    if (!rejected(graph, "undriven")) return 1;
    graph = simple();
    auto hole = graph.wire(8, "loop");
    auto loop = graph.binary("add", hole, constant(1, 8), 8);
    graph.connect(hole, loop); graph.ports[1].bits = loop;
    if (!rejected(graph, "combinational cycle")) return 1;
    std::puts("synthesis rejection checks passed");
}
