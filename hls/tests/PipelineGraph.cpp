#include "../../synth/Verilog.h"
#include "../../synth/retiming.h"
#include "../../synth/Mapping.h"
#include <cstdio>

cpphdl::graph::Graph makeGraph();
int main(int argc, char** argv) {
    try {
        if (argc != 2) return 2;
        auto graph = makeGraph();
        std::ostringstream saved; graph.save(saved);
        cpphdl::graph::Graph loaded; loaded.load(saved.str().c_str());
        if (loaded.pipelines.size() != 1 || loaded.pipelines[0].logic != graph.pipelines.at(0).logic)
            throw std::runtime_error("streaming graph metadata did not round-trip");
        graph = std::move(loaded);
        graph.optimize();
        auto order = graph.dependencyOrder();
        std::set<std::string> arithmeticStages;
        bool contract = false;
        for (const auto& attr : graph.attributes)
            contract |= attr.name == "hls_pipeline_ii" && attr.value == "1";
        for (auto n : order) {
            const auto& node = graph.nodes[n];
            if (node.op == "add" || node.op == "mul" || node.op == "xor")
                if (node.scope.find(".stage_") != std::string::npos) arithmeticStages.insert(node.scope);
        }
        if (!contract || arithmeticStages.size() < 2)
            throw std::runtime_error("pipeline must split live arithmetic, not just delay the result");
        auto timed = graph;
        auto pass = cpphdl::synth::retime(timed,{"fit_pipeline_retiming",1000,""});
        if (pass.initiationInterval != 1 || pass.addedLatency || pass.feedbackScheduled)
            throw std::runtime_error("retiming changed the II=1 contract");
        bool rejected = false;
        try { cpphdl::synth::retime(timed,{"fit_pipeline_retiming",0.01,""}); }
        catch (const std::runtime_error& error) { rejected = std::string(error.what()).find("overhead") != std::string::npos; }
        if (!rejected) throw std::runtime_error("retiming must not silently serialize a pipeline");
        auto fitted = cpphdl::synth::retime(graph,{"fit_pipeline_retiming",1.5,""});
        if (!fitted.met || !fitted.addedLatency || !fitted.insertedBits ||
            fitted.initiationInterval != 1 || fitted.feedbackScheduled)
            throw std::runtime_error("streaming timing cuts must retain II=1");
        const auto latency = graph.pipelines.at(0).latency;
        auto again = cpphdl::synth::retime(graph,{"fit_pipeline_retiming",1.5,""});
        if (again.addedLatency) throw std::runtime_error("retiming an already fitted stream added stages");
        graph = cpphdl::synth::mapGates(std::move(graph));
        cpphdl::synth::emitVerilog(graph,argv[1],"PipelineTop");
        std::printf("LATENCY=%u estimated=%.3fns added=%u bits=%u\n",latency,fitted.after,fitted.addedLatency,fitted.insertedBits);
        std::printf("Pipeline graph: %zu arithmetic stages, II=1 contract retained\n",arithmeticStages.size());
        return 0;
    } catch (const std::exception& error) { std::fprintf(stderr,"%s\n",error.what()); return 1; }
}
