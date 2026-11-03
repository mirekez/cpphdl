#include "../../synth/PipelineReplacement.h"
#include <cassert>
#include <sstream>

int main() {
    using namespace cpphdl::graph;
    Graph graph;
    graph.clockContract=ClockContract::RisingEdgeStep;
    graph.currentScope="parent";
    StreamPipeline region;region.scope="parent.dsp";region.latency=7;
    for(auto name:{"reset","command_valid_in","response_ready_in","operation_in","index_in","value_in"})
        region.pins[name]=graph.add("input",1,{},{},{},name);
    for(auto name:{"command_ready_out","response_valid_out","result_out","fault_out"}) {
        auto bits=graph.wire(1,name,"state");region.pins[name]=bits;region.registers.push_back(bits);
        graph.states.push_back({bits,{0},{1}});
    }
    graph.pipelines.push_back(region);
    StreamPipeline other;other.scope="parent.other";graph.pipelines.push_back(other);
    graph.attributes.push_back({region.scope,"pipeline_replacement","TestReplacement"});
    graph.attributes.push_back({region.scope,"pipeline_replacement_body","module TestReplacement; endmodule"});
    graph.attributes.push_back({region.scope,"pipeline_replacement_min_latency","2"});
    cpphdl::synth::linkPipelineReplacements(graph);
    assert(graph.currentScope=="parent");assert(graph.states.empty());
    assert(graph.replacements.size()==1 && graph.replacements[0].latency==7);
    assert(graph.pipelines.size()==1 && graph.pipelines[0].scope==other.scope);
    auto count=graph.nodes.size();cpphdl::synth::linkPipelineReplacements(graph);assert(graph.nodes.size()==count);
    std::ostringstream stream;graph.save(stream);Graph loaded;loaded.load(stream.str().c_str());
    assert(loaded.replacements.size()==1);
    const auto& result=loaded.replacements.front();
    assert(result.module=="TestReplacement" && result.latency==7 && result.pins.size()==10);
    assert(result.verilog==graph.replacements.front().verilog);
}
