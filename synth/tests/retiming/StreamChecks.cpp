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
static Graph singleVisitStream(bool heldInputs, bool nested = false) {
    Graph logic,top;top.clockContract=ClockContract::RisingEdgeStep;
    auto input=logic.wire(8,"value","input");
    logic.currentScope="single.hls_stage_0";
    auto sum=logic.binary("add",input,constant(3,8),8);
    logic.currentScope="single.hls_stage_6";
    auto result=logic.binary("xor",sum,input,8);
    if(nested) {
        Graph child;child.currentScope="callee.hls_stage_0";
        auto a=child.wire(8,"a","input"),b=child.wire(8,"b","input"),c=child.wire(8,"c","input");
        auto early=child.binary("xor",a,constant(0x51,8),8);
        auto value=child.binary("xor",child.binary("xor",early,b,8),constant(0x51,8),8);
        child.ports={{"operation_in",a,true},{"index_in",b,true},{"value_in",c,true},
            {"result_out",value,false},{"fault_out",constant(0,32),false}};
        AtomicMethod method;method.argumentBits=8;method.resultBits=8;method.minimumLatency=8;
        std::ostringstream saved;child.save(saved);method.logic=saved.str();
        logic.attributes.push_back({"callee","atomic_method",method.serialize()});
        Value args=sum;args.insert(args.end(),input.begin(),input.end());args.resize(24,0);
        logic.currentScope="single.hls_stage_2";
        result=logic.add("pipeline_call",8,args,constant(0,64),{},"callee");
    }
    logic.ports={{"value_in",input,true},{"result_out",result,false},{"fault_out",constant(0,32),false}};
    if(heldInputs)logic.attributes.push_back({"single","hls_single_visit_inputs","1"});
    StreamPipeline region;region.scope="single";region.stages=nested?20:8;
    std::ostringstream saved;logic.save(saved);region.logic=saved.str();
    for(auto [pin,width]:std::map<std::string,unsigned>{{"reset",1},{"value_in",8},
            {"command_valid_in",1},{"response_ready_in",1}}) {
        region.pins[pin]=top.wire(width,pin,"input");
        top.ports.push_back({pin,region.pins.at(pin),true});
    }
    buildStreamPipeline(top,region,logic);
    for(const std::string pin:{"result_out","fault_out","command_ready_out","response_valid_out"})
        top.ports.push_back({pin,region.pins.at(pin),false});
    top.pipelines.push_back(std::move(region));return top;
}
static void simulateSingleVisit(Graph graph) {
    Evaluate sim(graph);bool pending=false;unsigned expected=0,completed=0,held=0,cancelled=0;
    for(unsigned cycle=0;cycle<5000;++cycle) {
        const bool reset=cycle==0 || cycle==97 || cycle==311;
        const bool submit=!reset&&!pending&&cycle%3!=0;
        const bool ready=cycle%59<43;
        const unsigned input=(cycle*37+11)&255;
        // The caller is free to change argument buses immediately after
        // admission. Captured inputs must survive bubbles and held results.
        sim.input("reset",reset);sim.input("value_in",input);
        sim.input("command_valid_in",submit);sim.input("response_ready_in",ready);sim.eval();
        if(reset) {cancelled+=pending;pending=false;}
        else {
            if(sim.output("response_valid_out")) {
                require(pending,"single-visit unsolicited response");
                require(!sim.output("fault_out") && sim.output("result_out")==expected,
                        "single-visit input changed through bubbles/stalls");
                if(ready){pending=false;++completed;}else ++held;
            }
            if(submit&&sim.output("command_ready_out")) {
                require(!pending,"single-visit test overlapped commands");
                pending=true;expected=((input+3)&255)^input;
            }
        }
        sim.tick();
    }
    require(completed>100 && held>0 && cancelled>0,"single-visit test missed completion/stall/reset coverage");
}
static void checkSingleVisit(bool nested) {
    auto ordinary=singleVisitStream(false,nested),held=singleVisitStream(true,nested);
    auto bits=[](const Graph& graph){size_t n=0;for(const auto& s:graph.states)n+=s.bits.size();return n;};
    require(bits(held)<bits(ordinary),"single-visit input alignment retained redundant registers");
    if(nested)require(bits(held)*2<bits(ordinary),"nested single-visit call retained alignment copies");
    simulateSingleVisit(ordinary);simulateSingleVisit(held);
    auto fit=retime(held,{"fit_pipeline_retiming",0.75,""});
    require(fit.met,"single-visit captured input failed retiming");simulateSingleVisit(held);
    std::ostringstream saved;held.save(saved);Graph loaded;loaded.load(saved.str().c_str());
    simulateSingleVisit(loaded);loaded=mapGates(std::move(loaded));
    fit=retime(loaded,{"fit_pipeline_retiming",0.65,""});
    require(fit.met,"mapped single-visit captured input failed retiming");simulateSingleVisit(loaded);
}
int main() {
    try {
        checkSingleVisit(false);checkSingleVisit(true);
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
        // A shallower mapper may fit by redistributing the existing stages.
        // Check the complete parent-fed path, rather than requiring extra
        // latency even when the original number of stages is sufficient.
        require(parentFit.met && parentFit.before>1.5 && parentFit.after<=1.5 &&
                parent.pipelines.front().generation>1,"parent input delay was not budgeted");
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
