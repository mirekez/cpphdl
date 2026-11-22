#pragma once
#include "cpphdl_graph.h"

namespace cpphdl::synth {
// Ordered DRAM responses carry the context of the accepted source call into
// its continuation. Requests and responses advance independently. Context RAM
// has a registered head and a synchronous look-ahead read, with forwarding
// when the next head is written on the same edge.
inline std::map<std::string,graph::Value> buildMemoryContinuation(
    graph::Graph& g,const std::string& scope,unsigned depth,
    const std::map<std::string,graph::Value>& in,bool fullExchange=true) {
    using namespace graph;
    if(!depth || depth>256 || (depth&(depth-1)))
        throw std::runtime_error("memory continuation depth must be a power of two in 1..256");
    const auto previousScope=g.currentScope;g.currentScope=scope;
    // A proven single-token site needs one payload register, not a wide,
    // almost-empty block RAM plus its registered/forwarded head copies.
    if(depth==1) {
        auto both=[&](Value a,Value b){return g.binary("and",a,b,1);};
        auto either=[&](Value a,Value b){return g.binary("or",a,b,1);};
        auto inv=[&](Value a){return g.unary("not",a);};
        auto occupied=g.wire(1,scope+".occupied","state");
        const auto reset=in.at("reset"),running=inv(reset);
        const auto payloadBits=in.at("context_in").size();
        const bool predicated=in.count("request_enable_in");
        auto issue=predicated?in.at("request_enable_in"):Value{1};
        auto context=in.at("context_in");if(predicated)context.push_back(issue.at(0));
        auto saved=g.wire(context.size(),scope+".context","state");
        auto enabled=predicated?slice(saved,payloadBits,1):Value{1};
        auto available=both(running,occupied);
        std::map<std::string,Value> out;
        out["response_valid_out"]=both(available,either(inv(enabled),in.at("memory_valid_in")));
        out["memory_ready_out"]=both(available,both(enabled,in.at("response_ready_in")));
        auto pop=both(out.at("response_valid_out"),in.at("response_ready_in"));
        auto space=fullExchange?either(inv(occupied),pop):inv(occupied);
        out["request_valid_out"]=both(running,both(space,both(issue,in.at("command_valid_in"))));
        out["command_ready_out"]=both(running,both(space,either(inv(issue),in.at("request_ready_in"))));
        auto push=both(in.at("command_valid_in"),out.at("command_ready_out"));
        out["context_out"]=slice(saved,0,payloadBits);
        out["data_out"]=g.mux(enabled,in.at("memory_data_in"),Value(in.at("memory_data_in").size(),0));
        out["error_out"]=both(enabled,in.at("memory_error_in"));
        g.states.push_back({occupied,g.mux(reset,Value{0},g.mux(push,Value{1},g.mux(pop,Value{0},occupied))),{1}});
        g.states.push_back({saved,g.mux(push,context,saved),{1}});
        g.currentScope=previousScope;return out;
    }
    // Two-entry elastic boundaries are tiny register queues, not block RAMs.
    // A direct registered head avoids RAM tCO plus forwarding/mux logic on
    // the high-fanout local-memory request and response boundary. Deeper
    // contexts below retain synchronous block-RAM storage.
    if(depth==2) {
        auto both=[&](Value a,Value b){return g.binary("and",a,b,1);};
        auto either=[&](Value a,Value b){return g.binary("or",a,b,1);};
        auto inv=[&](Value a){return g.unary("not",a);};
        auto occupied=g.wire(1,scope+".occupied","state");
        auto second=g.wire(1,scope+".second","state");
        const auto reset=in.at("reset"),running=inv(reset);
        const auto payloadBits=in.at("context_in").size();
        const bool predicated=in.count("request_enable_in");
        auto issue=predicated?in.at("request_enable_in"):Value{1};
        auto context=in.at("context_in");if(predicated)context.push_back(issue.at(0));
        auto head=g.wire(context.size(),scope+".head_context","state");
        auto tail=g.wire(context.size(),scope+".tail_context","state");
        auto enabled=predicated?slice(head,payloadBits,1):Value{1};
        auto available=both(running,occupied);
        std::map<std::string,Value> out;
        out["response_valid_out"]=both(available,either(inv(enabled),in.at("memory_valid_in")));
        out["memory_ready_out"]=both(available,both(enabled,in.at("response_ready_in")));
        auto pop=both(out.at("response_valid_out"),in.at("response_ready_in"));
        auto space=fullExchange?either(inv(second),pop):inv(second);
        out["request_valid_out"]=both(running,both(space,both(issue,in.at("command_valid_in"))));
        out["command_ready_out"]=both(running,both(space,either(inv(issue),in.at("request_ready_in"))));
        auto push=both(in.at("command_valid_in"),out.at("command_ready_out"));
        out["context_out"]=slice(head,0,payloadBits);
        out["data_out"]=g.mux(enabled,in.at("memory_data_in"),Value(in.at("memory_data_in").size(),0));
        out["error_out"]=both(enabled,in.at("memory_error_in"));
        auto loadHead=both(push,either(inv(occupied),both(pop,inv(second))));
        auto loadTail=both(push,both(occupied,either(inv(pop),second)));
        g.states.push_back({head,g.mux(loadHead,context,g.mux(both(pop,second),tail,head)),{1}});
        g.states.push_back({tail,g.mux(loadTail,context,tail),{1}});
        g.states.push_back({occupied,g.mux(reset,Value{0},g.mux(push,Value{1},g.mux(pop,second,occupied))),{1}});
        g.states.push_back({second,g.mux(reset,Value{0},g.mux(g.binary("xor",push,pop,1),
            g.mux(push,occupied,Value{0}),second)),{1}});
        g.currentScope=previousScope;return out;
    }
    unsigned pointerBits=0;while((1u<<pointerBits)<depth)++pointerBits;
    auto both=[&](Value a,Value b){return g.binary("and",a,b,1);};
    auto either=[&](Value a,Value b){return g.binary("or",a,b,1);};
    auto inv=[&](Value a){return g.unary("not",a);};
    auto reg=[&](const std::string& name,unsigned width){return g.wire(width,scope+"."+name,"state");};
    const auto reset=in.at("reset"),running=inv(reset);
    auto head=reg("head",pointerBits),tail=reg("tail",pointerBits),count=reg("count",pointerBits+1);
    const unsigned payloadBits=in.at("context_in").size();
    const bool predicated=in.count("request_enable_in");
    Value context=in.at("context_in");
    const unsigned contextBits=context.size();
    auto forwarded=reg("forwarded_context",contextBits),ramContext=reg("ram_context",contextBits);
    auto useForwarded=reg("use_forwarded",1);
    auto empty=g.binary("eq",count,constant(0,count.size()),1);
    auto available=both(running,inv(empty));
    auto saved=g.mux(useForwarded,forwarded,ramContext);
    // Request enable is protocol state, not payload. Reading it from the
    // context RAM puts the RAM's clock-to-output delay in the response-ready
    // path, which can extend through arbiters and other memory boundaries.
    // Keep this one bit per entry in registers while payload stays in RAM.
    Value enables,enabled{1};
    if(predicated) {
        enables=reg("request_enables",depth);
        std::vector<Value> values;
        for(unsigned i=0;i<depth;++i)values.push_back(slice(enables,i,1));
        for(unsigned bit=0;values.size()>1;++bit) {
            std::vector<Value> level;
            for(size_t i=0;i<values.size();i+=2)
                level.push_back(g.mux({head[bit]},values[i+1],values[i]));
            values=std::move(level);
        }
        enabled=values.front();
    }
    auto issue=predicated?in.at("request_enable_in"):Value{1};
    std::map<std::string,Value> out;
    out["response_valid_out"]=both(available,either(inv(enabled),in.at("memory_valid_in")));
    out["memory_ready_out"]=both(available,both(enabled,in.at("response_ready_in")));
    auto pop=both(out.at("response_valid_out"),in.at("response_ready_in"));
    auto space=inv(g.binary("eq",count,constant(depth,count.size()),1));
    if(fullExchange)space=either(space,pop);
    out["request_valid_out"]=both(running,both(space,both(issue,in.at("command_valid_in"))));
    out["command_ready_out"]=both(running,both(space,either(inv(issue),in.at("request_ready_in"))));
    auto push=both(in.at("command_valid_in"),out.at("command_ready_out"));
    if(predicated) {
        Value nextEnables;
        for(unsigned i=0;i<depth;++i) {
            auto write=both(push,g.binary("eq",tail,constant(i,pointerBits),1));
            auto bit=g.mux(write,issue,slice(enables,i,1));
            nextEnables.push_back(bit.at(0));
        }
        // Occupancy reset invalidates these bits; each accepted command
        // writes its own enable before that entry can become visible.
        g.states.push_back({enables,nextEnables,{1}});
    }
    out["context_out"]=slice(saved,0,payloadBits);
    out["data_out"]=g.mux(enabled,in.at("memory_data_in"),Value(in.at("memory_data_in").size(),0));
    out["error_out"]=both(enabled,in.at("memory_error_in"));
    auto increment=[&](Value value){return g.binary("add",value,constant(1,value.size()),value.size());};
    auto nextHead=increment(head),nextTail=increment(tail);
    auto state=[&](Value q,Value d){g.states.push_back({q,g.mux(reset,Value(q.size(),0),d),{1}});};
    state(head,g.mux(pop,nextHead,head));state(tail,g.mux(push,nextTail,tail));
    state(count,g.mux(g.binary("xor",push,pop,1),
        g.mux(push,increment(count),g.binary("sub",count,constant(1,count.size()),count.size())),count));
    const auto memory=g.memories.size();
    g.memories.push_back({scope+".contexts",contextBits,depth});
    g.memoryWrites.push_back({memory,tail,context,push});
    Value lookahead;
    for(unsigned offset=0;offset<contextBits;offset+=64) {
        auto word=g.add("memory_read",std::min(64u,contextBits-offset),nextHead,
                        constant(memory,64),constant(offset/64,64));
        lookahead.insert(lookahead.end(),word.begin(),word.end());
    }
    auto last=g.binary("eq",count,constant(1,count.size()),1);
    auto forward=both(push,either(empty,both(pop,last)));
    auto read=both(pop,inv(last));
    // Keep the synchronous RAM output register free of data forwarding and
    // reset muxes. Mixing those into its D input prevents block-RAM inference.
    // Validity comes from count; uninitialized RAM data is never exposed as a
    // valid context. Forward a newly written head through separate registers.
    g.states.push_back({ramContext,g.mux(read,lookahead,ramContext),{1}});
    g.states.push_back({forwarded,g.mux(forward,context,forwarded),{1}});
    state(useForwarded,g.mux(forward,Value{1},g.mux(read,Value{0},useForwarded)));
    g.currentScope=previousScope;
    return out;
}
} // namespace cpphdl::synth
