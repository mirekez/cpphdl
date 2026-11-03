"""Independent RAM commit lanes preserve snapshots, priorities and failed commits."""
import argparse
import os
from pathlib import Path
import re
import subprocess
import tempfile

p=argparse.ArgumentParser(description=__doc__)
p.add_argument('--cxx',default='g++')
p.add_argument('--work',type=Path)
p.add_argument('--tsan',action='store_true')
p.add_argument('--active-commits',action='store_true')
p.add_argument('--task-workers',type=int,nargs='+',default=[None],help='Runtime independent-task worker caps')
p.add_argument('--cases',nargs='+',choices=('serial','plain','keys','external','delta','coupled','overlap','tasks'),
               help='Restrict diagnostic runs to these cache modes')
a=p.parse_args()
tmp=tempfile.TemporaryDirectory(prefix='cpphdl-parallel-memory-') if a.work is None else None
w=Path(tmp.name) if tmp else a.work.resolve();w.mkdir(parents=True,exist_ok=True)
include=Path(__file__).resolve().parents[2]/'include'
(w/'emit.cpp').write_text(r'''
#include "cpphdl_graph.h"
using namespace cpphdl::graph;
int main(int argc,char**argv) {
 Graph g;g.clockContract=ClockContract::NamedEdges;g.clocks.push_back({"clock",100});
 auto input=[&](const char*n,unsigned width){auto v=g.wire(width,n,"input");g.ports.push_back({n,v,true});return v;};
 auto address=input("address",5),data=input("data",96),enable=input("enable",3),bad=input("bad",1),reset=input("reset",1);
 unsigned count=std::stoul(argv[5]);
 const bool coupled=std::string(argv[3])=="coupled";
 for(unsigned m=0;m<count;++m)g.memories.push_back({"ram"+std::to_string(m),96,16,{}});
 // Every memory consumes the OLD contents of another memory. Interleaved
 // ports collide on four addresses and cross seven-port helper boundaries.
 for(unsigned i=0;i<count*8;++i) {
  unsigned m=i%count;
  auto old=g.add("memory_read",64,address,constant((m+1)%count,64),constant(0,64));
  auto low=g.binary("add",g.binary("add",slice(old,0,32),slice(data,0,32),32),constant(i,32),32);
  Value value=low;auto high=slice(data,32,64);value.insert(value.end(),high.begin(),high.end());
  auto dest=g.binary("add",address,constant((i/count)%4,5),5);
  g.memoryWrites.push_back({m,dest,value,slice(enable,coupled?0:m%3,1),0,coupled?false:bool(m%2)});
 }
 for(unsigned m=0;m<count;++m) {
  auto read=g.add("memory_read",64,address,constant(m,64),constant(0,64));
  g.ports.push_back({"out"+std::to_string(m),read,false});
 }
 g.memories.push_back({"reject",32,1,{}});
 g.memoryWrites.push_back({count,bad,constant(1,32),bad,0,false});
 auto state=g.add("state",32,constant(0,32),{},{},"counter");
 g.states.push_back({state,g.binary("add",state,constant(1,32),32),{1},0,false,reset,constant(0,32)});
 g.ports.push_back({"counter",state,false});
 std::string kind=argv[3];bool memo=kind!="plain",delta=kind!="plain"&&kind!="keys";
 if(kind!="external"&&memo)g.attributes.push_back({"","native.owned_memories","1"});
 if(coupled)for(const auto* name:{"native.cache_simple_transactions","native.cache_memory_commits","native.cache_small_memory_commits"})
  g.attributes.push_back({"",name,"1"});
 if(kind=="serial")g.attributes.push_back({"","native.serial_memory_commits","1"});
 else g.attributes.push_back({"","native.parallel_memory_commits","1"});
 if(kind=="tasks")g.attributes.push_back({"","native.tasks","24"});
 if(argc>6)g.attributes.push_back({"","native.active_memory_commits","1"});
 g.emit(argv[1],7,std::stoul(argv[2]),kind=="overlap",true,memo,false,delta,std::stoul(argv[4]));
}
''')
(w/'run.cpp').write_text(r'''
#include "model.h"
#include <array>
#include <cassert>
#include <cstdio>
#include <thread>
using Model=cpphdl_native::Model;
void exercise(Model& g,unsigned seed) {
 std::array<std::array<std::array<uint64_t,2>,16>,RAMS> expected{};
 bool previous=false;unsigned counter=0;
 auto check=[&]{
  const std::vector<std::array<uint64_t,2>>* memories[]={MEMORY_POINTERS};
  for(unsigned m=0;m<RAMS;++m)for(unsigned row=0;row<16;++row)assert((*memories[m])[row]==expected[m][row]);
  assert(g.counter[0]==counter);
 };
 for(unsigned t=0;t<384;++t) {
  g.clock=(t/3)%2;g.address[0]=(t/9)%8;
  g.data={seed+t*991u,t*177u,seed+t*131u};g.enable[0]=(t/7)%8;g.reset[0]=t%53==0;
  if(g.clock&&!previous&&t%5==0) {
   g.bad[0]=1;
   for(unsigned retry=0;retry<2;++retry) {
    try{g.eval(true);assert(false);}catch(const std::out_of_range&){}
    check();
   }
   {auto copy=g;g=copy;}
   g.bad[0]=0;
  }
  auto old=expected;
  if(g.clock!=previous)for(unsigned i=0;i<RAMS*8;++i) {
   unsigned m=i%RAMS;
   if((COUPLED?false:bool(m%2))==!g.clock&&(g.enable[0]&(1u<<(COUPLED?0:m%3)))) {
    unsigned dest=g.address[0]+(i/RAMS)%4;
    uint32_t low=old[(m+1)%RAMS][g.address[0]][0]+g.data[0]+i;
    expected[m][dest]={uint64_t(low)|(uint64_t(g.data[1])<<32),g.data[2]};
   }
  }
  if(g.reset[0])counter=0;else if(g.clock&&!previous)++counter;
  previous=g.clock;
  if(t%29==0) {
   bool called=false;
   try {g.evaluate_with_host([&]{called=true;throw std::logic_error("host");});assert(false);}
   catch(const std::logic_error&) {assert(called);}
   g.eval(false);
  } else g.step();
  check();
  if(t%31==0){auto copy=g;g=copy;g.eval(false);check();}
 }
}
int main() {
 static_assert(Model::__cpphdl_memory_commit_lanes==EXPECTED_LANES);
 Model g;exercise(g,1);
 // Independent copies share the persistent executor, not model state. The
 // caller can reach the commit phase while another copy prepares its inputs.
 Model seed,left=seed,right=seed;
 std::thread a([&]{exercise(left,123);}),b([&]{exercise(right,456);});
 a.join();b.join();
 puts("PASS parallel RAM: cross-memory snapshots, wide priority, clocks/reset, rejection/retry, concurrent copies");
}
''')
flags=[a.cxx,'-std=c++23','-O1','-pthread','-I'+str(include)]
subprocess.run([*flags,w/'emit.cpp','-o',w/'emit'],check=True)
cases=[('serial',6,1),('plain',6,0),('keys',6,1),('external',6,0),('delta',6,1),('delta',1,0),('coupled',6,1),('overlap',6,1),('tasks',6,1)]
if a.cases:cases=[c for c in cases if c[0] in a.cases]
elif a.tsan:cases=[('delta',6,1),('coupled',6,1),('overlap',6,1),('tasks',6,1)]
for index,(kind,threads,split) in enumerate(cases):
 d=w/f'{index}-{kind}';d.mkdir(exist_ok=True)
 subprocess.run([w/'emit',d/'model.h',str(threads),kind,str(split),'8',*(['active'] if a.active_commits else [])],check=True)
 generated=(d/'model.h').read_text()
 lanes=int(re.search(r'__cpphdl_memory_commit_lanes = (\d+)',generated)[1])
 if a.active_commits and lanes>1 and kind!='overlap':assert '__cpphdl_active_memory_commits = true;' in generated
 if kind=='coupled':assert 1<=lanes<=6 and '__cpphdl_cached_memory_commits = 0;' not in generated
 elif kind=='overlap':assert 1<lanes<=6
 else:assert lanes==(1 if kind=='serial' or threads==1 else 6)
 source=(w/'run.cpp').read_text().replace('MEMORY_POINTERS',','.join(f'&g.memory{m}' for m in range(8)))
 (d/'run.cpp').write_text(source)
 instrumentation=['-fsanitize=thread','-g'] if a.tsan else []
 subprocess.run([*flags,*instrumentation,'-DRAMS=8',f'-DCOUPLED={int(kind=="coupled")}',f'-DEXPECTED_LANES={lanes}','-I'+str(d),d/'run.cpp',
                 *sorted(d.glob('model.part.*.cpp')),'-o',d/'run'],check=True)
 for cap in a.task_workers:
  for dispatch in ('serial','parallel','auto'):
   env=dict(os.environ,CPPHDL_NATIVE_DISPATCH=dispatch)
   if cap is not None:env['CPPHDL_NATIVE_TASK_WORKERS']=str(cap)
   subprocess.run([d/'run'],env=env,check=True,timeout=60)
