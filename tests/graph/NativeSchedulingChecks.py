"""Compare chunked scheduling with the monolithic executor across clock phases."""
import argparse
from pathlib import Path
import subprocess
import tempfile

p = argparse.ArgumentParser()
p.add_argument('--cxx', required=True)
p.add_argument('--threaded', action='store_true')
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
#include <cassert>
using namespace cpphdl::graph;
void independent(Graph& graph,const NativeParallelPlan& plan) {
 std::vector<int> owner(graph.nodes.size(),-1);
 for(size_t task=0;task<plan.tasks.size();++task) {
  assert(plan.waits[task].empty());
  for(auto node:plan.tasks[task]) { assert(owner[node]<0);owner[node]=task; }
 }
 for(size_t task=0;task<plan.tasks.size();++task)for(auto index:plan.tasks[task]) {
  const auto&node=graph.nodes[index];
  for(auto*bits:{&node.left,&node.right,&node.select})for(auto raw:*bits) {
   auto bit=graph.resolve(raw);if(bit<2)continue;auto src=Graph::owner(bit);
   if(graph.nodes[src].op!="input"&&graph.nodes[src].op!="state")assert(owner[src]==int(task));
  }
 }
 for(auto index:graph.dependencyOrder())
  if(graph.nodes[index].op!="input"&&graph.nodes[index].op!="state")assert(owner[index]>=0);
}
int main(int argc,char**argv) {
 if(argc>3) {
  // Tiny and uneven cones still schedule every operation exactly once.
  for(unsigned count=1;count<=9;++count) for(unsigned heavy=0;heavy<count;++heavy) {
   Graph tiny;auto input=tiny.wire(64,"input","input");Value scattered;
   for(unsigned bit=0;bit<64;++bit)scattered.push_back(input[(bit*17)%64]);
   for(unsigned i=0;i<count;++i) {
    auto value=tiny.add("xor",64,i==heavy?scattered:input,constant(i+1,64));
    tiny.ports.push_back({"out"+std::to_string(i),value,false});
   }
   auto plan=nativeParallelPlan(tiny,tiny.dependencyOrder(),std::stoul(argv[3]));
   std::set<size_t> scheduled;
   for(const auto& task:plan.tasks)for(auto node:task)assert(scheduled.insert(node).second);
   assert(scheduled.size()==count);independent(tiny,plan);
  }
  Graph shared;auto common=shared.wire(64,"input","input");
  for(unsigned i=0;i<8;++i)common=shared.binary("add",common,constant(17*i+1,64),64);
  for(unsigned branch=0;branch<4;++branch) {
   auto value=common;
   for(unsigned i=0;i<32;++i)value=shared.binary("xor",shared.binary("add",value,constant(i+branch+1,64),64),common,64);
   shared.ports.push_back({"out"+std::to_string(branch),value,false});
  }
  auto before=shared.nodes.size();
  auto plan=nativeParallelPlan(shared,shared.dependencyOrder(),std::stoul(argv[3]));
  assert(shared.nodes.size()>before);independent(shared,plan);
 }
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
 g.emit(argv[1],std::stoul(argv[2]),argc>3?std::stoul(argv[3]):1);
}
''')
print(run([args.cxx,'-std=c++23','-O1','-I'+str(root/'include'),work/'emit.cpp','-o',work/'emit']))
for chunk in (0, 3, 19):
    print(run([work/'emit',work/f'model{chunk}.h',str(chunk)]))
if args.threaded:
    for threads in (2, 3, 4):
        print(run([work/'emit', work/f'threads{threads}.h', '19', str(threads)]))
(work/'check.cpp').write_text(r'''
#define cpphdl_native reference
#include "model0.h"
#undef cpphdl_native
#define cpphdl_native small
#include "model3.h"
#undef cpphdl_native
#include "model19.h"
#ifdef CHECK_THREADS
#define cpphdl_native parallel2
#include "threads2.h"
#undef cpphdl_native
#define cpphdl_native parallel3
#include "threads3.h"
#undef cpphdl_native
#define cpphdl_native parallel4
#include "threads4.h"
#undef cpphdl_native
#include <thread>
#include <cassert>
#endif
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
#ifdef CHECK_THREADS
 parallel2::Model p2;parallel3::Model p3;parallel4::Model p4;
 auto copied=p2;
 static_assert(parallel2::Model::__cpphdl_thread_count==2);
 static_assert(parallel3::Model::__cpphdl_thread_count==3);
 static_assert(parallel4::Model::__cpphdl_thread_count==4);
#endif
 for(unsigned i=0;i<8192;++i) {
  // Repeated inputs, changed inputs, commit and noncommit evaluations.
  if(i%7==0){random^=random<<13;random^=random>>7;random^=random<<17;}
  inputs(r,random,i);inputs(a,random,i);inputs(b,random,i);
#ifdef CHECK_THREADS
  inputs(p2,random,i);inputs(p3,random,i);inputs(p4,random,i);inputs(copied,random,i);
#endif
  for(bool commit:{false,true,false}) {
   r.eval(commit);a.eval(commit);b.eval(commit);
   if(!same(r,a)||!same(r,b)){std::printf("mismatch at %u commit=%d\n",i,commit);return 1;}
#ifdef CHECK_THREADS
   p2.eval(commit);p3.eval(commit);p4.eval(commit);copied.eval(commit);
   assert(same(r,p2)&&same(r,p3)&&same(r,p4)&&same(r,copied));
#endif
  }
 }
#ifdef CHECK_THREADS
 // Distinct models sharing an executor may be evaluated by distinct callers.
 // Their worker jobs serialize; each model's state/outputs remain independent.
 auto one=p2,two=p2;auto ref1=r,ref2=r;
 auto check=[](auto& reference,auto& model,unsigned seed) {
  for(unsigned i=0;i<256;++i) {
   inputs(reference,i*7919+seed,i);inputs(model,i*7919+seed,i);
   reference.step();model.step();assert(same(reference,model));
  }
 };
 std::thread first([&]{check(ref1,one,13);}),second([&]{check(ref2,two,91);});
 first.join();second.join();
 std::puts("native threads: 2/3/4 lanes, shared-executor copies and concurrent callers PASS");
#endif
 std::puts("native scheduling: 24576 phase evaluations, RAM ordering and reset PASS");
}
''')
print(run([args.cxx,'-std=c++23','-O2','-I'+str(root/'include'),
           *(['-DCHECK_THREADS','-pthread','-fsanitize=undefined','-fno-sanitize-recover=all'] if args.threaded else []),
           work/'check.cpp','-o',work/'check']))
print(run([work/'check']))
