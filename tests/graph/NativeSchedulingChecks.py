"""Compare chunked scheduling with the monolithic executor across clock phases."""
import argparse
from pathlib import Path
import subprocess
import tempfile

p = argparse.ArgumentParser()
p.add_argument('--cxx', required=True)
args = p.parse_args()
root = Path(__file__).resolve().parents[2]
work = Path(tempfile.mkdtemp(prefix='cpphdl-native-scheduling-'))
def run(argv):
    result = subprocess.run(list(map(str, argv)), capture_output=True, text=True, timeout=180)
    if result.returncode:
        raise RuntimeError(result.stdout + result.stderr)
    return result.stdout

(work/'emit.cpp').write_text(r'''
#include <cpphdl_graph_native.h>
using namespace cpphdl::graph;
int main(int argc,char**argv) {
 Graph g;g.clockContract=ClockContract::NamedEdges;g.clocks.push_back({"clock",100});
 auto input=[&](const char*n,unsigned w){auto v=g.wire(w,n,"input");g.ports.push_back({n,v,true});return v;};
 auto data=input("data",64),addr=input("address",4),enable=input("enable",1),reset=input("reset",1);
 auto state=g.wire(64,"counter","state");
 auto falling=g.wire(64,"falling","state");
 g.memories.push_back({"ram",64,16});
 g.memories.push_back({"rom",64,4,{1,2,3,4}});
 auto read=g.add("memory_read",64,addr,constant(0,64),constant(0,64));
 auto rom=g.add("memory_read",64,slice(addr,0,2),constant(1,64),constant(0,64));
 auto chain=g.binary("add",read,data,64);
 // Shared values, nested muxes, and consumers separated by many chunks.
 Value saved;
 for(unsigned i=0;i<83;++i) {
  auto next=g.binary("xor",g.binary("add",chain,constant(i+1,64),64),state,64);
  chain=g.mux(slice(data,i%64,1),next,g.binary("add",chain,rom,64));
  if(i==17) saved=chain;
 }
 // A guard can itself depend on data from the guarded cone. Its ancestors
 // must be scheduled eagerly, including across chunk boundaries.
 auto choice=g.binary("lt",chain,saved,1);
 auto sum=g.mux(choice,g.binary("add",chain,saved,64),g.binary("xor",saved,data,64));
 g.states.push_back({state,g.mux(enable,sum,state),{1},0,false,reset,constant(7,64)});
 g.states.push_back({falling,g.binary("xor",data,read,64),{1},0,true});
 g.memoryAccesses.push_back({0,addr,enable,true,0,false});
 auto writeEnable=g.binary("and",enable,g.binary("lt",read,data,1),1);
 g.memoryWrites.push_back({0,addr,g.binary("add",data,state,64),writeEnable,0,false});
 // Ordered writes must preserve the second write when both are enabled.
 g.memoryWrites.push_back({0,addr,falling,g.binary("and",enable,slice(data,0,1),1),0,false});
 g.ports.push_back({"result",g.mux(enable,sum,falling),false});
 g.ports.push_back({"counter",state,false});g.ports.push_back({"read",read,false});
 g.ports.push_back({"early",saved,false});
 g.emit(argv[1],std::stoul(argv[2]));
}
''')
print(run([args.cxx,'-std=c++23','-O1','-I'+str(root/'include'),work/'emit.cpp','-o',work/'emit']))
for chunk in (0, 3, 19):
    print(run([work/'emit',work/f'model{chunk}.h',str(chunk)]))
(work/'check.cpp').write_text(r'''
#define cpphdl_native reference
#include "model0.h"
#undef cpphdl_native
#define cpphdl_native small
#include "model3.h"
#undef cpphdl_native
#include "model19.h"
#include <cstdio>
template<class M> void inputs(M& m,uint64_t data,unsigned i) {
 m.data={uint32_t(data),uint32_t(data>>32)};m.address[0]=(i/9)%16;
 m.enable[0]=(i/23)%2;m.reset[0]=(i%193)==0;m.clock=(i/3)%2;
}
template<class M> bool same(const reference::Model&r,const M&m) {
 return r.result==m.result && r.counter==m.counter && r.read==m.read && r.early==m.early && r.memory0==m.memory0;
}
int main() {
 reference::Model r;small::Model a;cpphdl_native::Model b;uint64_t random=42;
 for(unsigned i=0;i<8192;++i) {
  // Repeated inputs, changed inputs, commit and noncommit evaluations.
  if(i%7==0){random^=random<<13;random^=random>>7;random^=random<<17;}
  inputs(r,random,i);inputs(a,random,i);inputs(b,random,i);
  for(bool commit:{false,true,false}) {
   r.eval(commit);a.eval(commit);b.eval(commit);
   if(!same(r,a)||!same(r,b)){std::printf("mismatch at %u commit=%d\n",i,commit);return 1;}
  }
 }
 std::puts("native scheduling: 24576 phase evaluations, RAM ordering and reset PASS");
}
''')
print(run([args.cxx,'-std=c++23','-O2',work/'check.cpp','-o',work/'check']))
print(run([work/'check']))
