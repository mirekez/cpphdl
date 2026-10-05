#include "BlackBox.h"
#include "Mapping.h"
#include "Verilog.h"
#include "retiming.h"
#include <fstream>
#include <iterator>

using namespace cpphdl::graph;
using namespace cpphdl::synth;
static void require(bool condition) { if (!condition) throw std::runtime_error("blackbox regression failed"); }

int main(int argc, char** argv) {
    if (argc != 2) return 2;
    require(!blackBoxSpec({"something else"}));
    require(blackBoxSpec({"CPPHDL_BLACKBOX=external_math:0"})->delayPs == 0);
    require(blackBoxSpec({"CPPHDL_BLACKBOX=external_math:0.1231"})->delayPs == 124);
    for (auto text : {"missing", ":0", "bad-name:0", "x:-1", "x:nan", "x:inf", "x:0 trailing"}) {
        bool failed = false;
        try { blackBoxSpec({std::string("CPPHDL_BLACKBOX=") + text}); }
        catch (const std::exception&) { failed = true; }
        require(failed);
    }
    Graph graph;
    graph.clockContract = ClockContract::RisingEdgeStep;
    auto a = graph.wire(64, "a", "input");
    auto first = graph.add("blackbox", 16, a, constant(0, 64), {}, "external_first");
    auto second = graph.add("blackbox", 16, a, constant(0, 64), {}, "external_second");
    graph.currentScope = "opaque_constant_scope";
    auto constantCall = graph.add("blackbox", 16, constant(13, 64), constant(375, 64), {}, "external_constant");
    graph.currentScope.clear();
    // Both construction-time folding and later optimization must keep the
    // opaque invocation, including the scope needed by synthesis retiming.
    require(!number(graph.resolved(constantCall)));
    require(graph.nodes[graph.owner(constantCall[0])].scope == "opaque_constant_scope");
    graph.ports = {{"a", a, true}, {"first", first, false}, {"second", second, false}, {"constant_result", constantCall, false}};
    graph.optimize();
    require(graph.resolved(first) != graph.resolved(second));
    require(!number(graph.resolved(constantCall)));
    require(estimateTiming(graph).worst >= .375);
    auto mapped = mapGates(graph);
    unsigned boxes = 0;
    for (auto n : mapped.dependencyOrder()) if (mapped.nodes[n].op == "blackbox") ++boxes;
    require(boxes == 3);
    emitVerilog(mapped, argv[1], "BlackBoxes");
    std::ifstream file(argv[1]);
    std::string text{std::istreambuf_iterator<char>(file), {}};
    require(text.find("keep_hierarchy") != std::string::npos);
    require(text.find("external_first") != std::string::npos && text.find("external_second") != std::string::npos);
    auto state = graph.wire(16, "result_reg", "state");
    graph.states.push_back({state, constantCall, {1}});
    graph.ports = {{"a", a, true}, {"result", state, false}};
    bool rejected = false;
    try { retime(graph, {"fit_pipeline_retiming", .5, ""}); }
    catch (const std::exception& error) {
        rejected = std::string(error.what()).find("indivisible cell exceeds target period: blackbox") != std::string::npos;
        if (!rejected) std::fprintf(stderr, "unexpected timing rejection: %s\n", error.what());
    }
    require(rejected);
    std::puts("PASS: blackbox annotation, preservation, constant inputs, distinct identities and delay budget");
}
