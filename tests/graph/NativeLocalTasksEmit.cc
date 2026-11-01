#include "cpphdl_graph.h"
using namespace cpphdl::graph;
int main(int argc,char**argv) {
 Graph g;g.clockContract=ClockContract::RisingEdgeStep;
 auto input=[&](const char*n,unsigned width){auto v=g.wire(width,n,"input");g.ports.push_back({n,v,true});return v;};
 auto data=input("data",32),enable=input("enable",24),address=input("address",4),reset=input("reset",1);
 std::vector<std::vector<Value>> states(24);
 for(unsigned m=0;m<24;++m) {
  g.currentScope="unit"+std::to_string(m);
  for(unsigned r=0;r<8;++r)states[m].push_back(g.add("state",32,constant(0,32),{},{},"r"+std::to_string(r)));
  g.memories.push_back({g.currentScope+"/ram",32,16,{}});
 }
 Value outputs;
 for(unsigned m=0;m<24;++m) {
  g.currentScope="unit"+std::to_string(m);
  auto read=g.add("memory_read",32,address,constant((m+1)%24,64),constant(0,64));
  auto y=g.binary("xor",g.binary("add",states[m][0],data,32),read,32);
  for(unsigned k=0;k<96;++k)
   y=g.binary("xor",g.binary("mul",y,constant(1664525,32),32),
       g.binary("add",g.binary("shr",y,constant(3,32),32),constant(k+m+1013904223u,32),32),32);
  auto en=slice(enable,m,1),trigger=g.binary("or",en,reset,1);
  for(unsigned r=0;r<8;++r) {
   auto next=g.binary("add",y,states[(m+1)%24][r],32);
   g.states.push_back({states[m][r],g.mux(reset,constant(0,32),next),trigger});
   outputs.insert(outputs.end(),states[m][r].begin(),states[m][r].end());
  }
  for(unsigned r=0;r<4;++r)
   g.memoryWrites.push_back({m,g.binary("add",address,constant(r,4),4),
     g.binary("add",y,constant(r,32),32),en,-1,false});
 }
 g.ports.push_back({"result",outputs,false});
 for(const auto* option:{"native.owned_memories","native.guard_chunk_calls","native.cache_simple_transactions","native.parallel_memory_commits"})
  g.attributes.push_back({"",option,"1"});
 if(std::stoul(argv[3]))g.attributes.push_back({"","native.scope_tasks","1"});
 g.attributes.push_back({"","native.tasks",argv[2]});
 g.emit(argv[1],64,std::stoul(argv[4]),false,true,true,false,true);
}
