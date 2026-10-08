#include "StreamPipeline.h"
#include "retiming.h"
#include "Mapping.h"
#include "../Evaluate.h"
#include <cstdio>

using namespace cpphdl::graph;
using namespace cpphdl::synth;

static void require(bool condition, const char* message) {
    if (!condition) throw std::runtime_error(message);
}
static void addStream(Graph& top, const std::string& name, bool parentLogic = false) {
    Graph logic;
    auto input = logic.wire(8,"value","input");
    auto state = logic.wire(8,"state","state");
    logic.currentScope = name + ".hls_stage_0";
    auto sum = logic.binary("add",state,input,8);
    logic.currentScope = name + ".hls_stage_1";
    auto product = logic.binary("mul",sum,constant(3,8),8);
    logic.currentScope = name + ".hls_stage_2";
    auto next = logic.binary("xor",product,constant(0x59,8),8);
    logic.states = {{state,next,{1},-1,false,{},constant(17,8)}};
    logic.ports = {{"value_in",input,true},{"result_out",next,false},{"fault_out",constant(0,32),false}};
    StreamPipeline region; region.scope = name; region.stages = 3;
    std::ostringstream saved; logic.save(saved); region.logic = saved.str();
    top.currentScope = name;
    for (auto [pin,width] : std::map<std::string,unsigned>{{"reset",1},{"value_in",8},
             {"command_valid_in",1},{"response_ready_in",1}}) {
        region.pins[pin] = top.wire(width,name+"."+pin,"input");
        top.ports.push_back({name+"_"+pin,region.pins[pin],true});
    }
    if (parentLogic) region.pins["value_in"] = top.binary("add",region.pins["value_in"],constant(7,8),8);
    buildStreamPipeline(top,region,logic);
    for (const std::string pin : {"result_out","fault_out","command_ready_out","response_valid_out"})
        top.ports.push_back({name+"_"+pin,region.pins.at(pin),false});
    top.pipelines.push_back(std::move(region));
}
static void simulate(Graph graph) {
    Evaluate sim(graph);
    struct Reference { unsigned state = 17; std::vector<bool> valid; std::vector<unsigned> result; };
    std::vector<Reference> refs;
    for (const auto& p : graph.pipelines) refs.push_back({17,std::vector<bool>(p.latency),std::vector<unsigned>(p.latency)});
    for (unsigned cycle = 0; cycle < 500; ++cycle) {
        for (size_t k = 0; k < refs.size(); ++k) {
            const auto prefix = graph.pipelines[k].scope + "_";
            sim.input(prefix+"reset",cycle == 0 || cycle == 257);
            sim.input(prefix+"value_in",(cycle*37+k*11)&255);
            sim.input(prefix+"command_valid_in",cycle < 150 || cycle%3 != 0);
            sim.input(prefix+"response_ready_in",cycle < 150 || cycle%61 < 35);
        }
        sim.eval();
        for (size_t k = 0; k < refs.size(); ++k) {
            const auto prefix = graph.pipelines[k].scope + "_";
            auto& r = refs[k];
            if (cycle == 0 || cycle == 257) { r.state=17; std::fill(r.valid.begin(),r.valid.end(),false); continue; }
            bool ready = cycle < 150 || cycle%61 < 35;
            bool valid = cycle < 150 || cycle%3 != 0;
            bool advance = !r.valid.back() || ready;
            require(sim.output(prefix+"command_ready_out") == advance,"stream admission mismatch");
            require(sim.output(prefix+"response_valid_out") == r.valid.back(),"stream valid mismatch");
            if (r.valid.back()) require(sim.output(prefix+"result_out") == r.result.back(),"floating feedback mismatch");
            if (advance) {
                unsigned value = (cycle*37+k*11)&255;
                if (graph.pipelines[k].scope == "parent_input") value = (value+7)&255;
                unsigned result = (((r.state+value)*3)&255)^0x59;
                if (r.valid[r.valid.size()-2]) r.state = r.result[r.result.size()-2];
                for (unsigned i=r.valid.size()-1;i;--i) { r.valid[i]=r.valid[i-1]; r.result[i]=r.result[i-1]; }
                r.valid[0]=valid; r.result[0]=result;
            }
        }
        sim.tick();
    }
}
int main() {
    try {
        Graph graph; graph.clockContract=ClockContract::RisingEdgeStep;
        addStream(graph,"left"); addStream(graph,"right");
        simulate(graph);
        auto original=graph;
        bool rejected=false;
        try { retime(graph,{"keep_behaviour_retiming",0.75,"left"}); }
        catch (const std::runtime_error&) { rejected=true; }
        require(rejected && graph.nodes.size()==original.nodes.size(),"keep mode altered stream latency");
        rejected=false;
        try { retime(graph,{"fit_pipeline_retiming",0.75,"left.schedule_0"}); }
        catch (const std::runtime_error&) { rejected=true; }
        require(rejected && graph.nodes.size()==original.nodes.size(),"partial region rule was not rejected atomically");
        auto fit=retime(graph,{"fit_pipeline_retiming",0.75,"left"});
        require(fit.met && fit.addedLatency && !fit.feedbackScheduled && fit.initiationInterval==1,"scoped fit broke stream contract");
        require(graph.pipelines[1].latency==3,"scoped rule changed other stream");
        simulate(graph);
        auto leftLatency=graph.pipelines[0].latency;
        retime(graph,{"fit_pipeline_retiming",0.85,""});
        require(graph.pipelines[0].latency==leftLatency,"looser parent rule shortened an existing pipeline");
        fit=retime(graph,{"fit_pipeline_retiming",0.65,""});
        require(fit.met && graph.pipelines[1].latency>3,"whole design fit did not include second stream");
        simulate(graph);
        std::ostringstream saved; graph.save(saved);
        Graph loaded; loaded.load(saved.str().c_str()); simulate(loaded);
        // Mapping must retain enough bindings to retime a loaded region again.
        graph=mapGates(std::move(loaded));
        fit=retime(graph,{"fit_pipeline_retiming",0.60,""});
        require(fit.met && fit.initiationInterval==1,"mapped stream cannot be retimed");
        simulate(graph);
        auto named=original; named.clockContract=ClockContract::NamedEdges; named.clocks={{"clock",100}};
        for (auto& state:named.states) state.clock=0;
        retime(named,{"fit_pipeline_retiming",0.75,""});
        for (const auto& state:named.states) require(state.clock==0,"stream lost clock ownership");
        simulate(named);
        Graph parent; parent.clockContract=ClockContract::RisingEdgeStep;
        addStream(parent,"parent_input",true);
        auto parentFit=retime(parent,{"fit_pipeline_retiming",1.5,""});
        require(parentFit.met && parentFit.addedLatency,"parent input delay was not budgeted");
        simulate(parent);
        auto mixed=original; mixed.clockContract=ClockContract::NamedEdges; mixed.clocks={{"a",100},{"b",50}};
        for (auto& state:mixed.states) state.clock=0;
        mixed.states.front().clock=1; rejected=false;
        try { retime(mixed,{"fit_pipeline_retiming",0.75,"left"}); }
        catch (const std::runtime_error&) { rejected=true; }
        require(rejected,"mixed-clock pipeline region was accepted");
        std::puts("stream retiming: floating feedback, scopes, independent regions, serialization, mapping, reset and stalls passed");
    } catch (const std::exception& e) { std::fprintf(stderr,"%s\n",e.what()); return 1; }
}
