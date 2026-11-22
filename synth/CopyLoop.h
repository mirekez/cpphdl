#pragma once
#include "ReadPipeline.h"

namespace cpphdl::synth {
// Pipeline iterations of an ordinary C++ external-to-local copy loop. This is
// a scheduler transformation, not a replacement for the source computation:
// the original body still goes through buildReadPipeline and normal retiming.
// Only memory-independent iteration control can run ahead. External reads are
// side-effect-free under ClockedMemory's streaming contract; local stores
// retire in order, and the loop exit waits for every issued iteration.
inline bool buildCopyLoopPipeline(graph::Graph& out,graph::Graph logic,
    const std::string& scope,unsigned depth,unsigned argumentBits,unsigned dataBits,
    unsigned resultBits,int entry,const std::map<std::string,graph::Value>& nextInputs,
    std::map<std::string,graph::Value>& pins) {
    using namespace graph;
    auto reject=[&](const std::string& reason) {
        out.attributes.push_back({scope,"hls_copy_loop_rejection",reason});return false;
    };
    if(depth<2 || !logic.states.empty() || !logic.memoryWrites.empty())return reject("unsupported depth or state");
    logic.optimize();
    auto order=logic.dependencyOrder();
    std::vector<size_t> accesses;
    std::set<std::string> identities;
    for(auto id:order) {
        const auto& n=logic.nodes[id];
        if(n.hostEffect() || n.validation())return reject("observable effect");
        if(n.op=="external_load" || n.op=="external_store" ||
           n.op=="local_load" || n.op=="local_store")
            if(identities.insert(n.name).second)accesses.push_back(id);
    }
    if(accesses.size()!=2 || logic.nodes[accesses[0]].op!="external_load" ||
       logic.nodes[accesses[1]].op!="local_store") {
        std::string effects;
        for(auto id:accesses)effects+=(effects.empty()?"":",")+logic.nodes[id].op;
        return reject("accesses: "+effects);
    }
    auto nodeBits=[&](size_t id) {
        Value value;for(unsigned bit=0;bit<logic.nodes[id].width;++bit)value.push_back((id+1)*64+2+bit);
        return value;
    };
    // Prove that the source guards its local write after an external failure.
    // Prefix faults can then suppress later writes using the same C++ guard.
    Graph failed=logic,control=logic;
    for(auto id:order)if(logic.nodes[id].op=="external_fault" || logic.nodes[id].op=="local_fault") {
        control.connect(nodeBits(id),constant(0,32));
        failed.connect(nodeBits(id),constant(logic.nodes[id].op=="external_fault"?5:0,32));
    }
    const auto storeGuard=logic.nodes[accesses[1]].select;
    failed.ports={{"guard",storeGuard,false}};failed.optimize();
    if(failed.resolved(failed.ports.front().bits)!=Value{0}) {
        if(const char* directory=std::getenv("CPPHDL_HLS_COPY_DIAGNOSTICS")) {
            std::ofstream original(std::string(directory)+"/"+scope+".original.graph");
            std::ofstream proof(std::string(directory)+"/"+scope+".failed.graph");
            if(!original || !proof)throw std::runtime_error("cannot write copy-loop diagnostic");
            logic.save(original);
            for(const auto& p:logic.ports)if(p.input)failed.ports.push_back(p);
            failed.save(proof);
        }
        return reject("store is not guarded by the read fault");
    }
    Value destination;
    for(const auto& p:logic.ports)if(p.name=="result_out" && !p.input)
        destination=slice(p.bits,resultBits,32);
    if(destination.empty())return reject("missing destination");
    struct Input {std::string name;unsigned offset,width;Value bits,next;};
    std::vector<Input> inputs;unsigned frameBits=0;
    for(const auto& p:logic.ports)if(p.input) {
        auto found=nextInputs.find(p.name);if(found==nextInputs.end())return reject("missing feedback for "+p.name);
        inputs.push_back({p.name,frameBits,unsigned(p.bits.size()),p.bits,found->second});
        frameBits+=p.bits.size();
    }
    Value controlPacket=destination;
    for(const auto& i:inputs)controlPacket.insert(controlPacket.end(),i.bits.begin(),i.bits.end());
    for(const auto& i:inputs)controlPacket.insert(controlPacket.end(),i.next.begin(),i.next.end());
    control.ports.erase(std::remove_if(control.ports.begin(),control.ports.end(),
        [](const Port& p){return !p.input;}),control.ports.end());
    control.ports.push_back({"result_out",controlPacket,false});
    control.ports.push_back({"fault_out",constant(0,32),false});
    control.optimize();
    for(auto id:control.dependencyOrder()) {
        const auto& n=control.nodes[id];
        if(n.op=="external_load" || n.op=="external_store" || n.op=="external_fault" ||
           n.op=="local_load" || n.op=="local_store" || n.op=="local_fault" ||
           n.op=="atomic_call" || n.op=="pipeline_call" || n.hostEffect() || n.validation())
            return reject("iteration control depends on "+n.op+" "+n.name);
    }
    auto both=[&](Value a,Value b){return out.binary("and",a,b,1);};
    auto either=[&](Value a,Value b){return out.binary("or",a,b,1);};
    auto inv=[&](Value a){return out.unary("not",a);};
    out.currentScope=scope;
    for(auto [name,width]:std::map<std::string,unsigned>{{"reset",1},{"command_valid_in",1},
        {"response_ready_in",1},{"operation_in",argumentBits},{"index_in",argumentBits},
        {"value_in",argumentBits},{"memory_out.ready_out",1},{"memory_out.valid_out",1},
        {"memory_out.data_out",dataBits},{"memory_out.error_out",1},{"local_out.ready_out",1},
        {"local_out.valid_out",1},{"local_out.data_out",dataBits},{"local_out.error_out",1}})
        pins[name]=out.wire(width,scope+"."+name);
    for(const auto& i:inputs)if(!pins.count(i.name))pins[i.name]=out.wire(i.width,scope+"."+i.name);
    auto active=out.wire(1,scope+".copy.active","state");
    auto issuedLast=out.wire(1,scope+".copy.issued_last","state");
    // Bound admitted iterations, not just accepted memory requests. This also
    // bounds speculative work after a fault and avoids a 32-bit counter on
    // the high-fanout ready path. Queue/backpressure correctness does not rely
    // on any particular latency selected by the calculation retimer.
    const unsigned window=std::max(32u,4*depth);
    unsigned countBits=1;while((uint64_t(1)<<countBits)<=window)++countBits;
    auto pending=out.wire(countBits,scope+".copy.pending","state");
    auto start=out.wire(1,scope+".copy.start");
    auto feedback=out.wire(1,scope+".copy.feedback");
    auto body=buildReadPipeline(out,std::move(logic),scope+".copy.body",depth,argumentBits,dataBits,true,false,true);
    out.connect(body.at("reset"),either(pins.at("reset"),start));
    StreamPipeline issue;issue.scope=scope+".copy.iteration";
    issue.pins["reset"]=pins.at("reset");
    issue.pins["command_valid_in"]=either(start,feedback);
    const auto fault=body.at("ordered_copy_fault");
    const auto capacity=out.binary("lt",pending,constant(window,countBits),1);
    const auto bodyReady=both(capacity,body.at("command_ready_out"));
    issue.pins["response_ready_in"]=either(fault,bodyReady);
    std::map<std::string,Value> feedbackInputs;
    for(const auto& i:inputs) {
        auto value=out.wire(i.width,scope+".copy.feedback."+i.name);
        feedbackInputs[i.name]=value;
        issue.pins[i.name]=out.mux(active,value,pins.at(i.name));
    }
    compactPipelineLogic(control);
    std::ostringstream serialized;control.save(serialized);issue.logic=serialized.str();
    buildStreamPipeline(out,issue,std::move(control));out.pipelines.push_back(issue);
    // An idle wrapper has no iteration token: the previous exit waited for
    // the last prediction and all body replies. Do not feed body readiness
    // through the predictor back into start, since start resets the body.
    pins["command_ready_out"]=both(inv(pins.at("reset")),inv(active));
    out.connect(start,both(pins.at("command_valid_in"),pins.at("command_ready_out")));
    const auto packet=issue.pins.at("result_out");
    const auto again=out.binary("eq",slice(packet,0,32),constant(entry+1,32),1);
    auto issueValid=issue.pins.at("response_valid_out");
    auto accepted=both(issueValid,bodyReady);
    auto push=both(accepted,inv(fault));
    out.connect(feedback,both(push,again));
    out.connect(body.at("command_valid_in"),both(capacity,both(issueValid,inv(fault))));
    for(const auto& i:inputs) {
        out.connect(body.at(i.name),slice(packet,32+i.offset,i.width));
        out.connect(feedbackInputs.at(i.name),slice(packet,32+frameBits+i.offset,i.width));
    }
    const auto finishIssuing=both(issueValid,either(fault,both(bodyReady,inv(again))));
    const auto last=both(issuedLast,out.binary("eq",pending,constant(1,countBits),1));
    pins["response_valid_out"]=both(body.at("response_valid_out"),last);
    const auto retire=both(pins.at("response_valid_out"),pins.at("response_ready_in"));
    const auto more=out.binary("lt",constant(1,countBits),pending,1);
    out.connect(body.at("response_ready_in"),either(more,both(last,pins.at("response_ready_in"))));
    const auto pop=both(body.at("response_valid_out"),body.at("response_ready_in"));
    out.currentScope=scope+".copy";
    out.states.push_back({active,out.mux(pins.at("reset"),Value{0},
        out.mux(start,Value{1},out.mux(retire,Value{0},active))),{1}});
    out.states.push_back({issuedLast,out.mux(either(pins.at("reset"),start),Value{0},
        out.mux(finishIssuing,Value{1},issuedLast)),{1}});
    auto changed=out.binary("xor",push,pop,1);
    auto delta=out.mux(push,constant(1,countBits),constant(uint64_t(-1),countBits));
    out.states.push_back({pending,out.mux(either(pins.at("reset"),start),constant(0,countBits),
        out.mux(changed,out.binary("add",pending,delta,countBits),pending)),{1}});
    for(auto field:{"result_out","fault_out"})pins[field]=body.at(field);
    for(auto prefix:{"memory_out.","local_out."}) {
        for(auto field:{"ready_out","valid_out","data_out","error_out"})
            out.connect(body.at(std::string(prefix)+field),pins.at(std::string(prefix)+field));
        for(auto field:{"valid_in","ready_in","addr_in","size_in","write_in","data_in"})
            pins[std::string(prefix)+field]=body.at(std::string(prefix)+field);
    }
    out.attributes.push_back({scope,"hls_copy_loop_pipeline","1"});
    out.attributes.push_back({scope,"hls_copy_loop_window",std::to_string(window)});
    out.attributes.push_back({issue.scope,"hls_iteration_feedback","1"});
    return true;
}
} // namespace cpphdl::synth
