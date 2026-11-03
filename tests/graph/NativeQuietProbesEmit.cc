#include "cpphdl_graph.h"
using namespace cpphdl::graph;
#ifndef QUIET_PROBE_ROUNDS
#define QUIET_PROBE_ROUNDS 8
#endif
int main(int argc, char** argv) {
    if(argc != 4) return 2;
    Graph g; g.clockContract=ClockContract::ExplicitEvents;
    auto input=[&](const char* name,unsigned width) {
        auto v=g.wire(width,name,"input"); g.ports.push_back({name,v,true}); return v;
    };
    auto seed=input("seed",32), enabled=input("enabled",256), tick=input("tick",1);
    auto write=input("write",1), read=input("read",1), fail=input("fail",1), bad=input("bad",1);
    auto history=g.add("state",1,{0},{},{},"event history");
    g.states.push_back({history,tick,{1}});
    Value counters;
    for(unsigned i=0;i<256;++i) {
        auto value=g.binary("add",seed,constant(i+1,32),32);
        for(unsigned j=0;j<QUIET_PROBE_ROUNDS;++j) {
            value=g.binary("xor",value,g.binary("shl",value,constant(13,32),32),32);
            value=g.binary("xor",value,g.binary("shr",value,constant(17,32),32),32);
            value=g.binary("xor",value,g.binary("shl",value,constant(5,32),32),32);
        }
        auto trigger=g.binary("and",slice(enabled,i,1),slice(value,0,1),1);
        auto state=g.add("state",32,constant(0,32),{},{},"counter"+std::to_string(i));
        g.states.push_back({state,g.binary("add",state,constant(1,32),32),trigger});
        counters.insert(counters.end(),state.begin(),state.end());
    }
    g.ports.push_back({"counters",counters,false});
    g.memories.push_back({"ram",32,1,{}});
    g.memoryWrites.push_back({0,bad,seed,write});
    g.memoryAccesses.push_back({0,bad,read,true});
    g.add("assert_failure",1,fail,{},{},"quiet probe assertion");
    // All three exceptional/transactional predicates follow bit 255.
    // Checking only the first published word must fail this regression.
    auto plain=g; plain.emit(std::string(argv[1])+"/plain.h",64);
    g.attributes.push_back({"","native.quiet_events","1"});
    const bool memo=std::stoul(argv[3]);
    if(memo) {
        g.attributes.push_back({"","native.owned_memories","1"});
        g.attributes.push_back({"","native.guard_chunk_calls","1"});
    }
    g.emit(std::string(argv[1])+"/model.h",64,std::stoul(argv[2]),false,true,memo,false,memo);
    // Empty activity and an unconditional trigger retain constant fast paths.
    Graph empty; empty.clockContract=ClockContract::ExplicitEvents;
    empty.attributes.push_back({"","native.quiet_events","1"});
    empty.emit(std::string(argv[1])+"/empty.h");
    Graph always; always.clockContract=ClockContract::ExplicitEvents;
    always.attributes.push_back({"","native.quiet_events","1"});
    auto state=always.add("state",1,{0},{},{},"unconditional");
    always.states.push_back({state,always.unary("not",state),{1}});
    always.emit(std::string(argv[1])+"/always.h");
}
