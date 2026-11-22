#pragma once
#include "StreamPipeline.h"
#include "ReadArbiter.h"
#include "ReadGroup.h"

namespace cpphdl::synth {
// Cut ordinary C++ dataflow at each memory access into retimable calculation
// pipelines and independent context queues. Only live values cross each cut;
// already computed source calls are carried forward, never duplicated.
inline std::map<std::string,graph::Value> buildReadPipeline(
    graph::Graph& out,graph::Graph logic,const std::string& scope,
    unsigned depth,unsigned argumentBits,unsigned dataBits,bool localMemory=false,bool singleVisit=false,
    bool orderedCopy=false) {
    using namespace graph;
    if(!logic.states.empty())throw std::runtime_error("streaming memory requires stateless source calculations");
    logic.optimize();
    auto order=logic.dependencyOrder();
    std::set<size_t> live(order.begin(),order.end()),done;
    std::vector<size_t> reads;
    std::map<std::string,size_t> loadIdentity;
    std::map<size_t,std::set<size_t>> loadWords;
    std::map<size_t,unsigned> loadOffsets;
    bool writes=false;
    for(auto n:live)if(logic.nodes[n].op=="external_load" || logic.nodes[n].op=="external_store" ||
                       logic.nodes[n].op=="local_load" || logic.nodes[n].op=="local_store") {
        const auto& node=logic.nodes[n];
        const bool store=node.op=="external_store" || node.op=="local_store";
        writes|=store;
        if(store){reads.push_back(n);continue;}
        unsigned offset=0;
        if(node.right.size()>8) {
            auto part=number(logic.resolved(slice(node.right,8,32)));
            if(!part || *part+node.width>dataBits)throw std::runtime_error("invalid wide memory word");
            offset=*part;
        }
        loadOffsets[n]=offset;
        auto [found,inserted]=loadIdentity.emplace(node.name,n);
        const auto leader=found->second;
        if(inserted)reads.push_back(n);
        else {
            const auto& first=logic.nodes[leader];
            if(first.op!=node.op || logic.resolved(first.left)!=logic.resolved(node.left) ||
               logic.resolved(first.select)!=logic.resolved(node.select) ||
               logic.resolved(slice(first.right,0,8))!=logic.resolved(slice(node.right,0,8)))
                throw std::runtime_error("inconsistent words of one memory access");
        }
        loadWords[leader].insert(n);
    }
    const unsigned headerBits=41+(writes?1+dataBits:0);
    std::map<std::string,Value> pins;
    out.currentScope=scope;
    for(auto [name,width]:std::map<std::string,unsigned>{{"reset",1},{"command_valid_in",1},
        {"response_ready_in",1},{"operation_in",argumentBits},{"index_in",argumentBits},
        {"value_in",argumentBits},{"memory_out.ready_out",1},{"memory_out.valid_out",1},
        {"memory_out.data_out",dataBits},{"memory_out.error_out",1}})
        pins[name]=out.wire(width,scope+"."+name);
    if(localMemory)for(auto [name,width]:std::map<std::string,unsigned>{{"ready_out",1},{"valid_out",1},
        {"data_out",dataBits},{"error_out",1}})pins["local_out."+name]=out.wire(width,scope+".local_out."+name);
    for(const auto& p:logic.ports)if(p.input && !pins.count(p.name))
        pins[p.name]=out.wire(p.bits.size(),scope+"."+p.name);
    if(singleVisit && localMemory && !writes && reads.size()>=2 && reads.size()<=32 &&
       std::all_of(reads.begin(),reads.end(),[&](size_t id){return logic.nodes[id].op=="local_load";}) &&
       buildIndependentLocalReadGroup(out,logic,scope,depth,dataBits,reads,loadWords,loadOffsets,pins))return pins;
    // Without a disjointness proof, mutable external memory orders successive
    // calls. This dependency token spans only the memory region: calculation
    // pipelines before and after it remain independently retimable. Read-only
    // calls retain their full request window. No application phase/FSM is used.
    Value busy,enter,leave;
    const bool serializeWrites=writes && !orderedCopy;
    if(serializeWrites)busy=out.wire(1,scope+".memory_dependency","state");
    // A proven loop containing just an external read followed by a local
    // write has no cross-iteration read/write alias. Its ordered continuations
    // preserve WAW order even when local destinations repeat. Carry a prefix
    // fault with each reply, so speculative later reads cannot perform stores
    // after the first failing read. Earlier successful stores remain enabled.
    Value copyFault;
    if(orderedCopy) {
        if(singleVisit || reads.size()!=2 || logic.nodes[reads[0]].op!="external_load" ||
           logic.nodes[reads[1]].op!="local_store")
            throw std::runtime_error("invalid ordered copy pipeline");
        copyFault=out.wire(1,scope+".copy_prefix_fault","state");
        pins["ordered_copy_fault"]=copyFault;
    }
    auto both=[&](Value a,Value b){return out.binary("and",a,b,1);};
    auto nodeValue=[&](size_t n) {
        Value value;
        for(unsigned bit=0;bit<logic.nodes[n].width;++bit) {
            Bit raw=(n+1)*64+2+bit;
            // Aliased lanes belong to their canonical producer, not this cut.
            value.push_back(logic.resolve(raw)==raw?raw:0);
        }
        return value;
    };
    std::map<size_t,Value> available;
    for(const auto& p:logic.ports)if(p.input)
        for(unsigned bit=0;bit<p.bits.size();++bit) {
            auto b=logic.resolve(p.bits[bit]);if(b<2)continue;
            auto n=Graph::owner(b);auto& value=available[n];
            if(value.empty())value.resize(logic.nodes[n].width,0);
            value[Graph::lane(b)]=pins.at(p.name).at(bit);done.insert(n);
        }
    auto regionLogic=[&](StreamPipeline& region,bool final) {
        Graph part=logic;part.ports.clear();
        for(const auto& [n,value]:available) {
            auto name="value_"+std::to_string(n);
            auto& node=part.nodes[n];node.op="input";node.name=name;
            node.left.clear();node.right.clear();node.select.clear();
            part.ports.push_back({name,nodeValue(n),true});region.pins[name]=value;
        }
        if(final)for(const auto& p:logic.ports)if(!p.input)part.ports.push_back(p);
        return part;
    };
    auto emit=[&](StreamPipeline& region,Graph part) {
        // A loop visit cannot submit a second token to this calculation until
        // its first token has retired. The scheduler may hold one captured
        // input instead of shifting identical context through later stages.
        if(singleVisit)part.attributes.push_back({region.scope,"hls_single_visit_inputs","1"});
        compactPipelineLogic(part);
        std::ostringstream serialized;part.save(serialized);region.logic=serialized.str();
        buildStreamPipeline(out,region,std::move(part));
        out.attributes.push_back({region.scope,"hls_pipeline_ii","1"});
        out.pipelines.push_back(region);
    };
    auto valid=pins.at("command_valid_in");
    Value upstreamReady;
    std::vector<std::map<std::string,Value>> endpoints,localEndpoints;
    for(unsigned index=0;index<reads.size();++index) {
        auto id=reads[index];const auto& read=logic.nodes[id];
        std::set<size_t> responseNodes{id};
        responseNodes.insert(loadWords[id].begin(),loadWords[id].end());
        const bool local=read.op=="local_load" || read.op=="local_store";
        if(local && !localMemory)throw std::runtime_error("local access requires a BRAM endpoint");
        for(auto n:live)if((logic.nodes[n].op=="external_fault" || logic.nodes[n].op=="local_fault") && logic.nodes[n].name==read.name)
            responseNodes.insert(n);
        StreamPipeline before;
        before.scope=scope+(reads.size()==1?".request":".request_"+std::to_string(index));
        auto queueReady=out.wire(1,before.scope+".queue_ready");
        before.pins={{"reset",pins.at("reset")},{"command_valid_in",valid},{"response_ready_in",queueReady}};
        auto request=regionLogic(before,false);
        const bool store=read.op=="external_store" || read.op=="local_store";
        Value packet=read.left;auto size=slice(read.right,0,8);
        packet.insert(packet.end(),size.begin(),size.end());
        packet.insert(packet.end(),read.select.begin(),read.select.end());
        if(writes) {
            packet.push_back(store?1:0);
            auto data=store?slice(read.right,8,read.right.size()-8):Value{};data.resize(dataBits,0);
            packet.insert(packet.end(),data.begin(),data.end());
        }
        request.ports.push_back({"result_out",packet,false});
        request.ports.push_back({"fault_out",constant(0,32),false});
        for(auto n:request.dependencyOrder()) {
            if(request.nodes[n].op=="external_load" || request.nodes[n].op=="external_store" || request.nodes[n].op=="external_fault" ||
               request.nodes[n].op=="local_load" || request.nodes[n].op=="local_store" || request.nodes[n].op=="local_fault")
                throw std::runtime_error("streaming access has a cyclic or unscheduled memory dependency");
            done.insert(n);
        }
        // Multiword source-call results come from one physical invocation.
        std::set<std::string> calls;
        for(auto n:done)if(logic.nodes[n].op=="pipeline_call" || logic.nodes[n].op=="atomic_call")
            calls.insert(logic.nodes[n].name);
        for(auto n:live)if((logic.nodes[n].op=="pipeline_call" || logic.nodes[n].op=="atomic_call") &&
                           calls.count(logic.nodes[n].name))done.insert(n);
        done.insert(responseNodes.begin(),responseNodes.end());
        std::set<size_t> frontier;
        auto require=[&](const Value& value) {
            for(auto bit:logic.resolved(value))if(bit>1 && done.count(Graph::owner(bit)))frontier.insert(Graph::owner(bit));
        };
        for(auto n:live)if(!done.count(n)) {
            const auto& node=logic.nodes[n];require(node.left);require(node.right);require(node.select);
        }
        for(const auto& p:logic.ports)if(!p.input)require(p.bits);
        std::map<size_t,unsigned> offsets;
        for(auto n:frontier)if(!responseNodes.count(n)) {
            // Loop regions admit one token until return. Their input frame and
            // each earlier access's saved context remain unchanged throughout
            // this visit. Reuse those registers instead of copying every live
            // scalar through every later arithmetic pipeline and memory queue.
            if(singleVisit && available.count(n))continue;
            offsets[n]=packet.size()-headerBits;
            auto value=nodeValue(n);packet.insert(packet.end(),value.begin(),value.end());
        }
        // A RAM-backed queue still needs a bit for a call with no live context.
        if(packet.size()==headerBits)packet.push_back(0);
        request.ports[request.ports.size()-2].bits=packet;
        emit(before,std::move(request));
        if(upstreamReady.empty())pins["command_ready_out"]=before.pins.at("command_ready_out");
        else out.connect(upstreamReady,before.pins.at("command_ready_out"));
        auto result=before.pins.at("result_out");
        upstreamReady=out.wire(1,before.scope+".continuation_ready");
        std::map<std::string,Value> endpoint;
        for(auto [name,width]:std::map<std::string,unsigned>{{"request_ready_in",1},{"memory_valid_in",1},
            {"memory_data_in",dataBits},{"memory_error_in",1}})endpoint[name]=out.wire(width,before.scope+"."+name);
        auto queueInputs=endpoint;
        auto enabled=serializeWrites && index==0?out.unary("not",busy):Value{1};
        queueInputs.insert({{"reset",pins.at("reset")},{"command_valid_in",before.pins.at("response_valid_out")},
            {"context_in",slice(result,headerBits,result.size()-headerBits)},{"response_ready_in",upstreamReady}});
        queueInputs["command_valid_in"]=both(enabled,before.pins.at("response_valid_out"));
        if(logic.resolved(read.select)!=Value{1})queueInputs["request_enable_in"]=slice(result,40,1);
        // Shared request arbitration needs valid independent of downstream
        // ready: otherwise a full queue's same-edge pop feeds back through
        // the next stage's grant and this stage's own request-valid signal.
        // A single-visit site cannot receive its next token on the edge its
        // current reply retires. Do not propagate downstream pop readiness
        // backwards to request capacity when that exchange cannot occur.
        auto queue=buildMemoryContinuation(out,before.scope+".memory",singleVisit?1:depth,queueInputs,!singleVisit && reads.size()==1);
        out.connect(queueReady,both(enabled,queue.at("command_ready_out")));
        if(serializeWrites && index==0)enter=both(queueInputs.at("command_valid_in"),queue.at("command_ready_out"));
        if(serializeWrites && index+1==reads.size())leave=both(queue.at("response_valid_out"),upstreamReady);
        endpoint["addr"]=slice(result,0,32);endpoint["size"]=slice(result,32,8);
        endpoint["write"]=writes?slice(result,41,1):Value{0};
        endpoint["data"]=writes?slice(result,42,dataBits):Value(dataBits,0);
        endpoint["request_valid_out"]=queue.at("request_valid_out");
        endpoint["memory_ready_out"]=queue.at("memory_ready_out");
        (local?localEndpoints:endpoints).push_back(endpoint);
        Value responseData=queue.at("data_out"),responseError=queue.at("error_out");
        if(orderedCopy && index==0) {
            responseError=out.binary("or",responseError,copyFault,1);
            auto take=both(queue.at("response_valid_out"),upstreamReady);
            out.states.push_back({copyFault,out.mux(pins.at("reset"),Value{0},
                out.mux(take,responseError,copyFault)),{1}});
        }
        if(singleVisit) {
            // The external reply bus may change immediately after ACK. Retain
            // this site's data/error for later cuts, while presenting the live
            // reply to the continuation that accepts it on this same edge.
            // Unlike the bus, this register cannot be overwritten until the
            // region is revisited after its current result retires.
            unsigned replyBits=0;
            if(!store)for(auto word:loadWords.at(id))replyBits=std::max(replyBits,loadOffsets.at(word)+logic.nodes[word].width);
            Value reply=slice(responseData,0,replyBits);reply.push_back(responseError.at(0));
            auto saved=out.wire(reply.size(),before.scope+".saved_reply","state");
            auto take=both(queue.at("response_valid_out"),upstreamReady);
            out.states.push_back({saved,out.mux(take,reply,saved),{1}});
            auto held=out.mux(queue.at("response_valid_out"),reply,saved);
            responseData=slice(held,0,replyBits);responseError=slice(held,replyBits,1);
        }
        auto previous=std::move(available);
        available.clear();
        for(auto n:frontier) {
            if(!store && loadWords.at(id).count(n))available[n]=slice(responseData,loadOffsets.at(n),logic.nodes[n].width);
            else if(n==id)available[n]=Value{0};
            else if(responseNodes.count(n))available[n]=out.mux(responseError,constant(local?3:5,32),constant(0,32));
            else if(singleVisit && previous.count(n))available[n]=previous.at(n);
            else available[n]=slice(queue.at("context_out"),offsets.at(n),logic.nodes[n].width);
        }
        valid=queue.at("response_valid_out");
    }
    StreamPipeline after;after.scope=scope+".continuation";
    after.pins={{"reset",pins.at("reset")},{"command_valid_in",valid},
                {"response_ready_in",pins.at("response_ready_in")}};
    auto continuation=regionLogic(after,true);emit(after,std::move(continuation));
    if(upstreamReady.empty())pins["command_ready_out"]=after.pins.at("command_ready_out");
    else out.connect(upstreamReady,after.pins.at("command_ready_out"));
    for(const auto& name:{"result_out","fault_out","response_valid_out"})pins[name]=after.pins.at(name);
    if(serializeWrites)out.states.push_back({busy,out.mux(pins.at("reset"),Value{0},
        out.mux(leave,Value{0},out.mux(enter,Value{1},busy))),{1}});
    connectReadStages(out,scope+".read_bus",depth,dataBits,endpoints,pins,singleVisit?1:depth,singleVisit);
    if(localMemory) {
        std::map<std::string,Value> localPins{{"reset",pins.at("reset")}};
        for(const auto& field:{"ready_out","valid_out","data_out","error_out"})
            localPins["memory_out."+std::string(field)]=pins.at("local_out."+std::string(field));
        connectReadStages(out,scope+".local_bus",depth,dataBits,localEndpoints,localPins,singleVisit?1:depth,singleVisit);
        for(const auto& field:{"valid_in","ready_in","addr_in","size_in","write_in","data_in"})
            pins["local_out."+std::string(field)]=localPins.at("memory_out."+std::string(field));
    }
    return pins;
}
} // namespace cpphdl::synth
