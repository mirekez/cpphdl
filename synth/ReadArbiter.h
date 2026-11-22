#pragma once
#include "MemoryContinuation.h"

namespace cpphdl::synth {
// Share an ordered external port. Accepted requests reserve per-stage reply
// storage, so a blocked continuation cannot hold up its own downstream reply.
inline void connectReadStages(graph::Graph& g,const std::string& scope,unsigned depth,
    unsigned dataBits,std::vector<std::map<std::string,graph::Value>>& stages,
    std::map<std::string,graph::Value>& pins,unsigned replyDepth=0,bool exclusiveRequests=false) {
    using namespace graph;
    pins["memory_out.write_in"]={0};pins["memory_out.data_in"]=Value(dataBits,0);
    if(stages.empty()) {
        pins["memory_out.valid_in"]={0};pins["memory_out.ready_in"]={0};
        pins["memory_out.addr_in"]=constant(0,32);pins["memory_out.size_in"]=constant(0,8);
    } else if(stages.size()==1) {
        auto& s=stages[0];
        g.connect(s.at("request_ready_in"),pins.at("memory_out.ready_out"));
        g.connect(s.at("memory_valid_in"),pins.at("memory_out.valid_out"));
        g.connect(s.at("memory_data_in"),pins.at("memory_out.data_out"));
        g.connect(s.at("memory_error_in"),pins.at("memory_out.error_out"));
        for(const auto& name:{"addr","size"})pins["memory_out."+std::string(name)+"_in"]=s.at(name);
        pins["memory_out.write_in"]=s.at("write");pins["memory_out.data_in"]=s.at("data");
        pins["memory_out.valid_in"]=s.at("request_valid_out");
        pins["memory_out.ready_in"]=s.at("memory_ready_out");
    } else {
        g.currentScope=scope;
        unsigned bits=1;while((size_t(1)<<bits)<stages.size())++bits;
        auto both=[&](Value a,Value b){return g.binary("and",a,b,1);};
        auto inv=[&](Value a){return g.unary("not",a);};
        auto equal=[&](Value a,unsigned b){return g.binary("eq",a,constant(b,bits),1);};
        Value next,locked,held;
        if(!exclusiveRequests) {
            next=g.wire(bits,scope+".next","state");
            locked=g.wire(1,scope+".locked","state");
            held=g.wire(bits,scope+".held","state");
        }
        auto reduceOr=[&](std::vector<Value> values,unsigned width) {
            while(values.size()>1) {
                std::vector<Value> level;
                for(size_t i=0;i<values.size();i+=2)
                    level.push_back(i+1==values.size()?values[i]:
                        g.binary("or",values[i],values[i+1],width));
                values=std::move(level);
            }
            return values.front();
        };
        // Two balanced priority trees implement round-robin choice: first
        // eligible index at/after next, otherwise wrap to the first request.
        // A priority chain for every possible start makes a wide source call's
        // memory backend grow quadratically and creates an unretimable path.
        struct Candidate {Value valid,index;};
        std::function<Candidate(unsigned,unsigned,bool)> first=[&](unsigned begin,unsigned count,bool eligible) -> Candidate {
            if(count==1) {
                if(begin>=stages.size())return {{0},constant(0,bits)};
                auto valid=stages[begin].at("request_valid_out");
                if(eligible)valid=both(valid,inv(g.binary("lt",constant(begin,bits),next,1)));
                return {valid,constant(begin,bits)};
            }
            auto left=first(begin,count/2,eligible),right=first(begin+count/2,count/2,eligible);
            return {g.binary("or",left.valid,right.valid,1),g.mux(left.valid,left.index,right.index)};
        };
        Value choice,valid;
        if(exclusiveRequests) {
            // A single-visit access chain cannot issue its next access until
            // the previous reply reaches that continuation. Its request
            // valids are one-hot. Do not encode a priority winner and decode
            // it back into every stage's ready path in this case.
            std::vector<Value> valids,owners;
            for(unsigned i=0;i<stages.size();++i) {
                auto v=stages[i].at("request_valid_out");valids.push_back(v);
                owners.push_back(g.mux(v,constant(i,bits),constant(0,bits)));
            }
            valid=reduceOr(std::move(valids),1);
            choice=reduceOr(std::move(owners),bits);
            g.attributes.push_back({scope,"hls_exclusive_memory_requests","1"});
        } else {
            auto any=first(0,1u<<bits,false),after=first(0,1u<<bits,true);
            choice=g.mux(after.valid,after.index,any.index);valid=any.valid;
            choice=g.mux(locked,held,choice);
        }
        auto select=[&](const std::string& field,unsigned width) {
            // Keep payload selection separate from the direct exclusive
            // ready grants. An indexed mux can eliminate identical fields
            // and shared address/data bits across access sites. Masking every
            // payload with its valid prevents that simplification and adds
            // wide gating even where all sites supply the same value.
            std::vector<Value> values(1u<<bits,Value(width,0));
            for(unsigned i=0;i<stages.size();++i)values[i]=stages[i].at(field);
            for(unsigned bit=0;values.size()>1;++bit) {
                std::vector<Value> level;
                for(size_t i=0;i<values.size();i+=2)level.push_back(g.mux({choice[bit]},values[i+1],values[i]));
                values=std::move(level);
            }
            return values.front();
        };
        // Each shared port is an elastic request boundary. Without it,
        // nested source-call arbiters concatenate their priority and payload
        // selection trees all the way to a RAM enable/input register. Capture
        // owner and payload together; reserve the response tag only when this
        // captured request is accepted by the next port. Full exchange keeps
        // one request per clock while downstream backpressure holds payload.
        Value packet=choice;
        for(auto field:{select("addr",32),select("size",8),select("write",1),select("data",dataBits)})
            packet.insert(packet.end(),field.begin(),field.end());
        auto issuedReady=g.wire(1,scope+".issued_ready");
        auto request=buildMemoryContinuation(g,scope+".request",1,
            {{"reset",pins.at("reset")},{"command_valid_in",valid},
             {"request_ready_in",{1}},{"context_in",packet},
             {"memory_valid_in",{1}},{"memory_data_in",{0}},
             {"memory_error_in",{0}},{"response_ready_in",issuedReady}});
        auto issued=request.at("context_out");
        auto replyReady=g.wire(1,scope+".reply_ready");
        auto tags=buildMemoryContinuation(g,scope+".owners",depth,
            {{"reset",pins.at("reset")},{"command_valid_in",request.at("response_valid_out")},
             {"request_ready_in",pins.at("memory_out.ready_out")},{"context_in",slice(issued,0,bits)},
             {"memory_valid_in",pins.at("memory_out.valid_out")},
             {"memory_data_in",pins.at("memory_out.data_out")},
             {"memory_error_in",pins.at("memory_out.error_out")},{"response_ready_in",replyReady}},false);
        // Do not use a retiring reply to open request capacity on this same
        // edge. That ready lookahead closes a combinational cycle through
        // response FIFOs, continuation pipelines and downstream requests.
        g.connect(issuedReady,tags.at("command_ready_out"));
        auto take=both(valid,request.at("command_ready_out"));
        auto state=[&](Value q,Value d){g.states.push_back({q,g.mux(pins.at("reset"),Value(q.size(),0),d),{1}});};
        if(!exclusiveRequests) {
            state(locked,both(valid,inv(take)));
            state(held,g.mux(valid,choice,held));
            auto successor=g.mux(equal(choice,stages.size()-1),constant(0,bits),
                                 g.binary("add",choice,constant(1,bits),bits));
            state(next,g.mux(take,successor,next));
        }
        std::vector<Value> replyReadyTerms;
        Value reply=tags.at("data_out");reply.push_back(tags.at("error_out").at(0));
        auto replyOwner=tags.at("context_out");
        auto replyValid=tags.at("response_valid_out");
        if(exclusiveRequests) {
            // A single-token chain cannot issue its next access before the
            // current reply is consumed. One shared registered reply slot
            // therefore suffices; separate per-site slots only duplicate its
            // wide data registers. Capture ownership with the payload and
            // retain the response boundary under downstream backpressure.
            Value packet=replyOwner;
            packet.insert(packet.end(),reply.begin(),reply.end());
            auto ready=g.wire(1,scope+".shared_response_ready");
            auto response=buildMemoryContinuation(g,scope+".shared_response",1,
                {{"reset",pins.at("reset")},{"command_valid_in",replyValid},
                 {"request_ready_in",{1}},{"context_in",packet},
                 {"memory_valid_in",{1}},{"memory_data_in",{0}},
                 {"memory_error_in",{0}},{"response_ready_in",ready}},false);
            g.connect(replyReady,response.at("command_ready_out"));
            replyReady=ready;
            replyOwner=slice(response.at("context_out"),0,bits);
            reply=slice(response.at("context_out"),bits,dataBits+1);
            replyValid=response.at("response_valid_out");
        }
        for(unsigned index=0;index<stages.size();++index) {
            auto& s=stages[index];
            auto selected=exclusiveRequests?s.at("request_valid_out"):equal(choice,index);
            auto owner=equal(replyOwner,index);
            g.connect(s.at("request_ready_in"),both(selected,request.at("command_ready_out")));
            if(exclusiveRequests) {
                replyReadyTerms.push_back(both(owner,s.at("memory_ready_out")));
                g.connect(s.at("memory_valid_in"),both(owner,replyValid));
                g.connect(s.at("memory_data_in"),slice(reply,0,dataBits));
                g.connect(s.at("memory_error_in"),slice(reply,dataBits,1));
                continue;
            }
            auto responses=buildMemoryContinuation(g,scope+".responses_"+std::to_string(index),replyDepth?replyDepth:depth,
                {{"reset",pins.at("reset")},{"command_valid_in",both(owner,tags.at("response_valid_out"))},
                 {"request_ready_in",{1}},{"context_in",reply},{"memory_valid_in",{1}},
                 {"memory_data_in",{0}},{"memory_error_in",{0}},
                 {"response_ready_in",s.at("memory_ready_out")}},replyDepth!=1);
            replyReadyTerms.push_back(both(owner,responses.at("command_ready_out")));
            g.connect(s.at("memory_valid_in"),responses.at("response_valid_out"));
            g.connect(s.at("memory_data_in"),slice(responses.at("context_out"),0,dataBits));
            g.connect(s.at("memory_error_in"),slice(responses.at("context_out"),dataBits,1));
        }
        while(replyReadyTerms.size()>1) {
            std::vector<Value> level;
            for(size_t i=0;i<replyReadyTerms.size();i+=2)level.push_back(i+1==replyReadyTerms.size()?replyReadyTerms[i]:
                g.binary("or",replyReadyTerms[i],replyReadyTerms[i+1],1));
            replyReadyTerms=std::move(level);
        }
        g.connect(replyReady,replyReadyTerms.front());
        pins["memory_out.addr_in"]=slice(issued,bits,32);pins["memory_out.size_in"]=slice(issued,bits+32,8);
        pins["memory_out.write_in"]=slice(issued,bits+40,1);pins["memory_out.data_in"]=slice(issued,bits+41,dataBits);
        pins["memory_out.valid_in"]=tags.at("request_valid_out");
        pins["memory_out.ready_in"]=tags.at("memory_ready_out");
    }
}
} // namespace cpphdl::synth
