#include "cpphdl_graph.h"
#ifndef NATIVE_SETTLE_ROUNDS
#define NATIVE_SETTLE_ROUNDS 32
#endif
using namespace cpphdl::graph;
int main(int argc,char** argv) {
    Graph g; g.clockContract=ClockContract::RisingEdgeStep;
    auto input=[&](const char* name,unsigned width) {
        auto value=g.wire(width,name,"input");g.ports.push_back({name,value,true});return value;
    };
    auto data=input("data",32), reset=input("reset",1);
    auto bad=input("bad",1), continuousBad=input("continuous_bad",1);
    std::vector<Value> state;
    for(unsigned i=0;i<24;++i) {
        g.currentScope="unit"+std::to_string(i);
        state.push_back(g.add("state",32,constant(0,32),{},{},"value"));
    }
    for(unsigned i=0;i<24;++i) {
        g.currentScope="unit"+std::to_string(i);
        auto value=g.binary("add",g.binary("xor",state[i],data,32),state[(i+1)%24],32);
        for(unsigned k=0;k<NATIVE_SETTLE_ROUNDS;++k) {
            value=g.binary("xor",value,g.binary("shl",value,constant(13,32),32),32);
            value=g.binary("xor",value,g.binary("shr",value,constant(17,32),32),32);
            value=g.binary("xor",value,g.binary("shl",value,constant(5,32),32),32);
        }
        g.states.push_back({state[i],g.mux(reset,constant(0,32),value),{1}});
    }
    g.ports.push_back({"observed",state[0],false});
    g.memories.push_back({"transaction_check",32,1,{}});
    g.memories.push_back({"continuous_check",32,1,{}});
    g.memoryAccesses.push_back({0,bad,{1},true});
    g.memoryAccesses.push_back({1,continuousBad,{1},false});
    // Several distinct transactional cones ensure rejected errors also live
    // in omitted tasks, rather than only in always-retained caller task zero.
    for(unsigned extra=1;extra<=2;++extra) {
        auto memory=g.memories.size();
        g.memories.push_back({"transaction_check"+std::to_string(extra),32,extra+1,{}});
        g.memoryAccesses.push_back({memory,g.binary("add",bad,constant(extra,32),32),{1},true});
    }
    g.attributes.push_back({"","native.scope_tasks","1"});
    g.attributes.push_back({"","native.tasks","24"});
    if(std::stoul(argv[2]))g.attributes.push_back({"","native.all_settle_tasks","1"});
    const bool memo=std::stoul(argv[3]);
    if(memo)for(const char* option:{"native.owned_memories","native.guard_chunk_calls"})
        g.attributes.push_back({"",option,"1"});
    g.emit(argv[1],64,6,false,true,memo,false,memo,std::stoul(argv[4]));
}
