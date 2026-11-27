#include "ReplacementControl.h"
#include "Verilog.h"
#include <stdexcept>
#include <string>

using namespace cpphdl::graph;
using cpphdl::synth::replacementControlLoads;

static void require(bool condition, const char* message) {
    if (!condition) throw std::runtime_error(message);
}

static Graph sharedControl(unsigned instances, bool reset = false, int clock = -1,
                           bool falling = false, unsigned width = 1) {
    Graph g;
    g.clockContract = ClockContract::RisingEdgeStep;
    auto input = g.wire(width, "input", "input");
    auto state = g.wire(width, "launch", "state");
    State s{state, input, {1}};
    s.clock = clock;
    s.falling = falling;
    if (reset) {
        s.reset = g.wire(1, "reset", "input");
        s.resetValue = constant(0, width);
        g.ports.push_back({"reset", s.reset, true});
    }
    g.states.push_back(s);
    g.ports.push_back({"input", input, true});
    auto select = g.wire(1, "select", "input");
    g.ports.push_back({"select", select, true});
    auto control = g.binary("and", {state[0]}, select, 1);
    constexpr const char* body =
        "module ControlSink #(parameter LATENCY=1)(input clk, input command_valid_in, "
        "input response_ready_in, input reset, input operation_in, output response_valid_out); "
        "assign response_valid_out = 1'b0; endmodule\n";
    for (unsigned i = 0; i < instances; ++i) {
        PipelineReplacement r;
        r.scope = "sink_" + std::to_string(i);
        r.module = "ControlSink";
        r.verilog = body;
        r.pins = {{"command_valid_in", control}, {"response_ready_in", control}};
        g.replacements.push_back(std::move(r));
    }
    return g;
}

int main() {
    auto g = sharedControl(65);
    const auto launch = Graph::owner(g.states[0].bits[0]);
    auto loads = replacementControlLoads(g);
    require(loads.size() == 1 && loads.at(launch) == 65,
            "both handshake pins of one replacement counted twice");
    auto rtl = cpphdl::synth::verilogText(g, "ControlTop");
    const auto declaration = "`ifdef CPPHDL_TARGET_AGILEX7\n(* maxfan = 64 *)\n`endif\nreg [0:0] __cpphdl_node_" +
                             std::to_string(launch) + ";";
    require(rtl.find(declaration) != std::string::npos,
            "missing guarded maxfan assignment on shared launch register");
    require(rtl.find("(* maxfan = 64 *)", rtl.find(declaration) + declaration.size()) == std::string::npos,
            "unexpected additional maxfan assignment");
    require(rtl.find("preserve") == std::string::npos, "retiming blocked by preserve attribute");

    g = sharedControl(64);
    require(cpphdl::synth::verilogText(g, "ControlTop").find("(* maxfan = 64 *)") == std::string::npos,
            "threshold includes 64 consumers");
    for (auto excluded : {sharedControl(65, true), sharedControl(65, false, -1, false, 2)}) {
        require(replacementControlLoads(excluded).empty(), "unsafe register selected");
        require(cpphdl::synth::verilogText(excluded, "ControlTop").find("(* maxfan = 64 *)") == std::string::npos,
                "unsafe register received maxfan assignment");
    }
    // The named secondary clock is valid in a graph, but must never get a
    // default-clock launch hint.
    g = sharedControl(65, false, 1);
    g.clockContract = ClockContract::NamedEdges;
    g.clocks = {{"main_clk", 100}, {"other_clk", 25}};
    require(replacementControlLoads(g).empty(), "secondary-clock register selected");
    require(cpphdl::synth::verilogText(g, "ControlTop").find("(* maxfan = 64 *)") == std::string::npos,
            "secondary-clock register received maxfan assignment");
    g = sharedControl(65, false, 0, true);
    g.clockContract = ClockContract::NamedEdges;
    g.clocks = {{"main_clk", 100}};
    require(replacementControlLoads(g).empty(), "falling-edge register selected");
    require(cpphdl::synth::verilogText(g, "ControlTop").find("(* maxfan = 64 *)") == std::string::npos,
            "falling-edge register received maxfan assignment");

    g = sharedControl(65);
    for (auto& r : g.replacements) {
        r.pins.erase("command_valid_in");
        r.pins.erase("response_ready_in");
        r.pins["operation_in"] = g.states[0].bits;
    }
    require(replacementControlLoads(g).empty(), "payload fanout selected as handshake control");
    require(cpphdl::synth::verilogText(g, "ControlTop").find("(* maxfan = 64 *)") == std::string::npos,
            "payload fanout received maxfan assignment");
    g = sharedControl(65);
    for (auto& r : g.replacements) {
        r.pins.erase("command_valid_in");
        r.pins.erase("response_ready_in");
        r.pins["reset"] = g.states[0].bits;
    }
    require(replacementControlLoads(g).empty(), "reset-pin fanout selected as handshake control");
    require(cpphdl::synth::verilogText(g, "ControlTop").find("(* maxfan = 64 *)") == std::string::npos,
            "reset-pin fanout received maxfan assignment");
    g = sharedControl(65);
    auto replacementOutput = g.wire(1, "replacement_output", "state");
    for (auto& r : g.replacements) {
        r.pins["command_valid_in"] = replacementOutput;
        r.pins["response_ready_in"] = replacementOutput;
    }
    g.replacements.front().pins["response_valid_out"] = replacementOutput;
    require(replacementControlLoads(g).empty(), "replacement output selected as launch register");
    require(cpphdl::synth::verilogText(g, "ControlTop").find("(* maxfan = 64 *)") == std::string::npos,
            "replacement output received maxfan assignment");
}
