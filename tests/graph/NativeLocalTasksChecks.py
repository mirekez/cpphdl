"""Automatically grouped scopes and independent tasks preserve graph semantics.

--build-only creates the same fixture for optional external throughput timing.
"""
import argparse
import os
from pathlib import Path
import subprocess
import tempfile

p=argparse.ArgumentParser(description=__doc__)
p.add_argument('--cxx',default='g++')
p.add_argument('--work',type=Path)
p.add_argument('--build-only',action='store_true')
p.add_argument('--tsan',action='store_true')
a=p.parse_args()
tmp=tempfile.TemporaryDirectory(prefix='cpphdl-local-tasks-') if a.work is None else None
w=Path(tmp.name) if tmp else a.work.resolve();w.mkdir(parents=True,exist_ok=True)
include=Path(__file__).resolve().parents[2]/'include'
(w/'emit.cpp').write_text(r'''
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
''')
(w/'run.cpp').write_text(r'''
#include "model.h"
#include <array>
#include <cassert>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <thread>
using Model=cpphdl_native::Model;
struct Oracle {
 std::array<std::array<uint32_t,8>,24> state{};
 std::array<std::array<uint32_t,16>,24> ram{};
 void step(const Model& g) {
  auto old=state;auto mem=ram;
  for(unsigned m=0;m<24;++m) {
   uint32_t y=(old[m][0]+g.data[0])^mem[(m+1)%24][g.address[0]];
   for(unsigned k=0;k<96;++k)y=(y*1664525u)^((y>>3)+k+m+1013904223u);
   bool en=g.enable[0]&(1u<<m);
   if(en||g.reset[0])for(unsigned r=0;r<8;++r)state[m][r]=g.reset[0]?0:y+old[(m+1)%24][r];
   if(en)for(unsigned r=0;r<4;++r)ram[m][(g.address[0]+r)%16]=y+r;
  }
 }
 void check(const Model& g)const {
  for(unsigned m=0;m<24;++m)for(unsigned r=0;r<8;++r)assert(g.result[m*8+r]==state[m][r]);
 }
};
void inputs(Model& g,unsigned t,unsigned mode) {
 g.data[0]=t*1009;g.address[0]=(t/4)%16;g.reset[0]=(t%137)==0;
 g.enable[0]=mode==0?0:mode==1?1u<<((t/32)%24):mode==2?0xffffff:((0x155555u<<((t/32)%2))&0xffffff);
}
void check(Model& g,unsigned offset) {
 Oracle oracle;
 for(unsigned t=0;t<256;++t) {
  inputs(g,t+offset,(t/32)%4);oracle.step(g);g.step();oracle.check(g);
  if(t%31==0) {auto copy=g;g=copy;g.eval(false);oracle.check(g);}
 }
}
int main(int argc,char**argv) {
 static_assert(Model::__cpphdl_thread_count<=6);
 if(argc==1) {
  Model seed;check(seed,0);Model initial,left=initial,right=initial;
  std::thread a([&]{check(left,256);}),b([&]{check(right,512);});a.join();b.join();
  puts("PASS local tasks: independent oracle, cyclic state/RAM snapshots, reset, copies, shared executor");return 0;
 }
 unsigned mode=std::strtoul(argv[1],nullptr,10);Model g;uint64_t digest=0,clocks=0;
 auto start=std::chrono::steady_clock::now();double seconds=0;
 do {
  for(unsigned t=0;t<1024;++t){inputs(g,t,mode);g.step();digest=digest*0x100000001b3ull+g.result[0];}
  clocks+=1024;seconds=std::chrono::duration<double>(std::chrono::steady_clock::now()-start).count();
 }while(seconds<2.0);
 printf("{\"clocks\":%llu,\"seconds\":%.9f,\"digest\":\"%016llx\",\"tasks\":%u,\"workers\":%u}\n",
 (unsigned long long)clocks,seconds,(unsigned long long)digest,Model::__cpphdl_task_count,Model::__cpphdl_thread_count);
}
''')
flags=[a.cxx,'-std=c++23','-O1','-pthread','-I'+str(include)]
subprocess.run([*flags,w/'emit.cpp','-o',w/'emit'],check=True)
# A single worker with multiple tasks must also work with default chunks.
(w/'default-emit.cpp').write_text(r'''#include "cpphdl_graph.h"
using namespace cpphdl::graph;
int main(int argc,char**argv) {
 Graph g;auto data=g.wire(32,"data","input");g.ports.push_back({"data",data,true});
 for(unsigned i=1;i<5;++i) {
  auto s=g.add("state",32,constant(0,32),{},{},"s"+std::to_string(i));
  g.states.push_back({s,g.binary("xor",data,constant(i,32),32),{1}});
  g.ports.push_back({"out"+std::to_string(i),s,false});
 }
 g.attributes.push_back({"","native.tasks","24"});
 g.emit(argv[1],0,1);
}
''')
(w/'default-run.cpp').write_text(r'''#include "model.h"
#include <cassert>
int main(){cpphdl_native::Model g;static_assert(decltype(g)::__cpphdl_thread_count==1);for(unsigned t=0;t<100;++t){g.data[0]=t;g.step();assert(g.out1[0]==(t^1));assert(g.out4[0]==(t^4));}}
''')
default_dir=w/'default-chunks';default_dir.mkdir(exist_ok=True)
subprocess.run([*flags,w/'default-emit.cpp','-o',w/'default-emit'],check=True)
subprocess.run([w/'default-emit',default_dir/'model.h'],check=True)
subprocess.run([*flags,'-I'+str(default_dir),w/'default-run.cpp','-o',w/'default-run'],check=True)
subprocess.run([w/'default-run'],check=True)
variants=[('flat6',6,0,6),('scoped6',6,1,6),('scoped24',24,1,6),('flat24',24,0,6),('scoped1',24,1,1)]
if a.tsan:variants=[('scoped24',24,1,6)]
for name,tasks,scopes,workers in variants:
 d=w/name;d.mkdir(exist_ok=True)
 subprocess.run([w/'emit',d/'model.h',str(tasks),str(scopes),str(workers)],check=True)
 instrumentation=['-fsanitize=thread','-g'] if a.tsan else []
 subprocess.run([*flags,*instrumentation,'-I'+str(d),w/'run.cpp','-o',d/'run'],check=True)
 if not a.build_only:
  for dispatch in ('serial','parallel','auto'):
   subprocess.run([d/'run'],env=dict(os.environ,CPPHDL_NATIVE_DISPATCH=dispatch),check=True,timeout=120)
 print('BUILT',name,flush=True)
