"""Check active-trigger caching and bank coherence against an independent oracle."""
import argparse
import os
from pathlib import Path
import subprocess
import tempfile

p = argparse.ArgumentParser(description=__doc__)
p.add_argument('--cxx', default='g++')
a = p.parse_args()
include = Path(__file__).resolve().parents[2] / 'include'
with tempfile.TemporaryDirectory(prefix='cpphdl-state-transactions-') as tmp:
    w = Path(tmp)
    (w/'emit.cpp').write_text(r'''
#include "cpphdl_graph.h"
using namespace cpphdl::graph;
int main(int argc,char**argv) {
 Graph g;g.clockContract=ClockContract::ExplicitEvents;
 auto input=[&](const char*n,unsigned width){auto v=g.wire(width,n,"input");g.ports.push_back({n,v,true});return v;};
 auto clocks=input("clocks",2),reset=input("reset",1),data=input("data",32),enable=input("enable",1),bad=input("bad",1);
 std::vector<Value> edges;
 for(unsigned i=0;i<2;++i) {
  auto c=slice(clocks,i,1),prev=g.add("state",1,{0},{},{},"event history");
  g.states.push_back({prev,c,{1}});
  edges.push_back(g.binary("and",c,g.unary("not",prev),1));
 }
 Value outputs;
 for(unsigned i=0;i<32;++i) {
  auto s=g.add("state",32,constant(i,32),{},{},"word"+std::to_string(i));
  auto next=g.mux(enable,g.binary("add",data,constant(i,32),32),s);
  g.states.push_back({s,next,edges[i%2],-1,false,reset,constant(i+1000,32)});
  // Shared words with different active trigger keys must preserve priority.
  if(i%3==0)g.states.push_back({s,g.binary("add",data,constant(i+100,32),32),edges[1-i%2],-1,false,{},{}});
  // Last update can restore the original word after earlier preparation.
  if(i%5==0)g.states.push_back({s,s,g.binary("and",enable,edges[0],1),-1,false,{},{}});
  outputs.insert(outputs.end(),s.begin(),s.end());
 }
 g.ports.push_back({"result",outputs,false});
 g.memories.push_back({"ram",32,1,{}});
 g.memoryWrites.push_back({0,bad,data,bad,-1,false});
 g.attributes.push_back({"","native.owned_memories","1"});
 g.attributes.push_back({"","native.guard_chunk_calls","1"});
 if(std::string(argv[2])=="scan")g.attributes.push_back({"","native.scan_roots","1"});
 if(std::string(argv[2])=="uncached")g.attributes.push_back({"","native.uncached_state_transactions","1"});
 g.emit(argv[1],7,6,false,true,true,false,true,1);
}
''')
    (w/'run.cpp').write_text(r'''
#include "model.h"
#include <array>
#include <cassert>
#include <cstdio>
int main() {
 cpphdl_native::Model m;std::array<uint32_t,32> expected{};
 for(unsigned i=0;i<32;++i)expected[i]=i;
 unsigned previous=0;
 for(unsigned t=0;t<2048;++t) {
  const unsigned clocks=((t/3)&1)|(((t/11)&1)<<1);
  const bool reset=t%97<3,enable=t%13<4;
  m.clocks[0]=clocks;m.reset[0]=reset;m.enable[0]=enable;m.data[0]=(t/17)*13;
  if(t%41==0) {
   m.bad[0]=1;
   for(unsigned retry=0;retry<2;++retry) {
    try{m.step();assert(false);}catch(const std::out_of_range&){}
    auto copy=m;m=copy;
   }
   m.bad[0]=0;m.eval(false);assert(m.result==expected);
  }
  auto old=expected;const unsigned edges=clocks&~previous;
  for(unsigned i=0;i<32;++i) {
   if(reset)expected[i]=i+1000;
   else if((edges&(1u<<(i%2)))&&enable)expected[i]=m.data[0]+i;
   if(i%3==0&&(edges&(1u<<(1-i%2))))expected[i]=m.data[0]+i+100;
   if(i%5==0&&enable&&(edges&1))expected[i]=old[i];
  }
  previous=clocks;m.step();assert(m.result==expected);
  m.eval(false);assert(m.result==expected);
  if(t%43==0){auto copy=m;m=copy;}
 }
 puts("PASS state transactions: independent edges, active keys, reset, priority, idle banks, failed copies");
}
''')
    flags = [a.cxx, '-std=c++23', '-O1', '-pthread', '-I'+str(include)]
    subprocess.run([*flags, w/'emit.cpp', '-o', w/'emit'], check=True)
    for variant in ('scan', 'uncached', 'cached'):
        d=w/variant;d.mkdir()
        subprocess.run([w/'emit',d/'model.h',variant],check=True)
        generated=(d/'model.h').read_text()
        assert ('__cpphdl_cached_state_transactions = true' in generated)==(variant=='cached')
        subprocess.run([*flags,'-I'+str(d),w/'run.cpp',*sorted(d.glob('model.part.*.cpp')),'-o',d/'run'],check=True)
        for dispatch in ('serial','parallel','auto'):
            subprocess.run([d/'run'],env=dict(os.environ,CPPHDL_NATIVE_DISPATCH=dispatch),check=True,timeout=60)
