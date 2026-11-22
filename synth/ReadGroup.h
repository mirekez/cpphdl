#pragma once
#include "StreamPipeline.h"
#include "ReadArbiter.h"

namespace cpphdl::synth {
// A single source-region visit can issue independent local loads together.
// Each site reserves its own reply slot; the continuation joins all replies.
// There are no intervening stores, dependent addresses, or speculative external
// accesses. The compiler-owned local arena has no recoverable access faults.
inline bool buildIndependentLocalReadGroup(graph::Graph& out,graph::Graph logic,
    const std::string& scope,unsigned depth,unsigned dataBits,
    const std::vector<size_t>& reads,const std::map<size_t,std::set<size_t>>& words,
    const std::map<size_t,unsigned>& offsets,std::map<std::string,graph::Value>& pins) {
    using namespace graph;
    if(reads.size()<2 || reads.size()>32)return false;
    for(auto id:reads)if(logic.nodes.at(id).op!="local_load")return false;
    if(!logic.memoryWrites.empty())return false;
    for(const auto& node:logic.nodes)if(node.hostEffect()||node.validation())return false;
    std::set<size_t> seen;
    std::vector<size_t> pending;
    auto require=[&](const Value& value) {
        for(auto bit:logic.resolved(value))if(bit>1)pending.push_back(Graph::owner(bit));
    };
    for(auto id:reads) {
        const auto& node=logic.nodes[id];require(node.left);require(node.right);require(node.select);
    }
    while(!pending.empty()) {
        auto id=pending.back();pending.pop_back();if(!seen.insert(id).second)continue;
        const auto& node=logic.nodes.at(id);
        // Pure address/predicate calculations may be recomputed in the final
        // continuation, but a source call must never be duplicated this way.
        if(node.op=="local_load"||node.op=="local_store"||node.op=="local_fault"||
           node.op=="external_load"||node.op=="external_store"||node.op=="external_fault"||
           node.op=="pipeline_call"||node.op=="atomic_call")return false;
        require(node.left);require(node.right);require(node.select);
    }
    auto both=[&](Value a,Value b){return out.binary("and",a,b,1);};
    auto inv=[&](Value a){return out.unary("not",a);};
    auto all=[&](std::vector<Value> values) {
        while(values.size()>1) {
            std::vector<Value> next;
            for(size_t i=0;i<values.size();i+=2)
                next.push_back(i+1==values.size()?values[i]:both(values[i],values[i+1]));
            values=std::move(next);
        }
        return values.empty()?Value{1}:values.front();
    };
    auto bits=[&](size_t id) {
        Value result;for(unsigned bit=0;bit<logic.nodes[id].width;++bit)result.push_back((id+1)*64+2+bit);
        return result;
    };
    auto emit=[&](StreamPipeline& region,Graph part) {
        part.attributes.push_back({region.scope,"hls_single_visit_inputs","1"});
        compactPipelineLogic(part);
        std::ostringstream serialized;part.save(serialized);region.logic=serialized.str();
        buildStreamPipeline(out,region,std::move(part));out.pipelines.push_back(region);
        out.attributes.push_back({region.scope,"hls_pipeline_ii","1"});
    };
    StreamPipeline before;before.scope=scope+".request_group";
    before.pins=pins;
    auto issuedAll=out.wire(1,before.scope+".issued_all");
    before.pins["response_ready_in"]=issuedAll;
    Graph request=logic;
    request.ports.erase(std::remove_if(request.ports.begin(),request.ports.end(),
        [](const Port& p){return !p.input;}),request.ports.end());
    Value packet;
    for(auto id:reads) {
        const auto& node=logic.nodes[id];
        packet.insert(packet.end(),node.left.begin(),node.left.end());
        const auto size=slice(node.right,0,8);packet.insert(packet.end(),size.begin(),size.end());
        packet.insert(packet.end(),node.select.begin(),node.select.end());
    }
    request.ports.push_back({"result_out",packet,false});
    request.ports.push_back({"fault_out",constant(0,32),false});
    emit(before,std::move(request));
    pins["command_ready_out"]=before.pins.at("command_ready_out");
    const auto payload=before.pins.at("result_out");
    StreamPipeline after;after.scope=scope+".continuation";after.pins=pins;
    auto joined=out.wire(1,scope+".load_group.joined");
    auto retire=out.wire(1,scope+".load_group.retire");
    after.pins["command_valid_in"]=joined;
    Graph continuation=logic;
    std::vector<std::map<std::string,Value>> endpoints;
    std::vector<Value> issuedFlags,replies;
    for(unsigned i=0;i<reads.size();++i) {
        const auto id=reads[i];const auto site=scope+".load_group.site_"+std::to_string(i);
        out.currentScope=site;
        auto issued=out.wire(1,site+".issued","state");issuedFlags.push_back(issued);
        std::map<std::string,Value> endpoint;
        for(auto [name,width]:std::map<std::string,unsigned>{{"request_ready_in",1},{"memory_valid_in",1},
            {"memory_data_in",dataBits},{"memory_error_in",1}})endpoint[name]=out.wire(width,site+"."+name);
        auto inputs=endpoint;
        inputs.insert({{"reset",pins.at("reset")},{"context_in",Value{0}},
            {"command_valid_in",both(before.pins.at("response_valid_out"),inv(issued))},
            {"response_ready_in",retire},{"request_enable_in",slice(payload,i*41+40,1)}});
        auto queue=buildMemoryContinuation(out,site+".memory",1,inputs,false);
        auto pushed=both(inputs.at("command_valid_in"),queue.at("command_ready_out"));
        out.states.push_back({issued,out.mux(pins.at("reset"),Value{0},
            out.mux(retire,Value{0},out.mux(pushed,Value{1},issued))),{1}});
        replies.push_back(queue.at("response_valid_out"));
        endpoint["addr"]=slice(payload,i*41,32);endpoint["size"]=slice(payload,i*41+32,8);
        endpoint["write"]={0};endpoint["data"]=Value(dataBits,0);
        endpoint["request_valid_out"]=queue.at("request_valid_out");
        endpoint["memory_ready_out"]=queue.at("memory_ready_out");endpoints.push_back(endpoint);
        // The caller admits only one visit until the final result retires.
        // Preserve replies beyond the join edge for the retimed calculation,
        // whose single-visit input optimization can capture them at later stages.
        unsigned width=0;
        for(auto word:words.at(id))width=std::max(width,offsets.at(word)+logic.nodes[word].width);
        auto data=slice(queue.at("data_out"),0,width);
        auto saved=out.wire(data.size(),site+".saved_reply","state");
        out.states.push_back({saved,out.mux(retire,data,saved),{1}});
        const auto held=out.mux(queue.at("response_valid_out"),data,saved);
        for(auto word:words.at(id)) {
            const auto name="load_group_value_"+std::to_string(i)+"_"+std::to_string(offsets.at(word));
            after.pins[name]=slice(held,offsets.at(word),logic.nodes[word].width);
            auto& node=continuation.nodes[word];node.op="input";node.name=name;
            node.left.clear();node.right.clear();node.select.clear();
            continuation.ports.push_back({name,bits(word),true});
        }
    }
    out.connect(issuedAll,all(issuedFlags));
    out.connect(joined,both(issuedAll,all(replies)));
    emit(after,std::move(continuation));
    out.connect(retire,both(joined,after.pins.at("command_ready_out")));
    for(auto field:{"response_valid_out","result_out","fault_out"})pins[field]=after.pins.at(field);
    for(auto field:{"valid_in","ready_in","write_in"})pins["memory_out."+std::string(field)]={0};
    pins["memory_out.addr_in"]=constant(0,32);pins["memory_out.size_in"]=constant(0,8);
    pins["memory_out.data_in"]=Value(dataBits,0);
    std::map<std::string,Value> localPins{{"reset",pins.at("reset")}};
    for(auto field:{"ready_out","valid_out","data_out","error_out"})
        localPins["memory_out."+std::string(field)]=pins.at("local_out."+std::string(field));
    connectReadStages(out,scope+".local_bus",depth,dataBits,endpoints,localPins,1);
    for(auto field:{"valid_in","ready_in","addr_in","size_in","write_in","data_in"})
        pins["local_out."+std::string(field)]=localPins.at("memory_out."+std::string(field));
    out.attributes.push_back({scope,"hls_independent_local_loads",std::to_string(reads.size())});
    return true;
}
} // namespace cpphdl::synth
