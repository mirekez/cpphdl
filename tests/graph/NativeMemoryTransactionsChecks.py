"""Check grouped RAM transactions against a clock/priority oracle and old path."""
import argparse
import os
import re
from pathlib import Path
import subprocess
import tempfile

p = argparse.ArgumentParser(description=__doc__)
p.add_argument('--cxx', default='g++')
p.add_argument('--work', type=Path)
a = p.parse_args()
include = Path(__file__).resolve().parents[2]/'include'
temporary = tempfile.TemporaryDirectory(prefix='cpphdl-memory-transactions-') if a.work is None else None
w = Path(temporary.name) if temporary else a.work.resolve()
w.mkdir(parents=True, exist_ok=True)
(w/'emit.cpp').write_text(r'''
#include "cpphdl_graph.h"
using namespace cpphdl::graph;
int main(int argc,char**argv) {
 Graph g;g.clockContract=ClockContract::NamedEdges;g.clocks.push_back({"clock",100});
 auto input=[&](const char*n,unsigned width){auto v=g.wire(width,n,"input");g.ports.push_back({n,v,true});return v;};
 auto address=input("address",4),data=input("data",96),enable=input("enable",3);
 auto bad=input("bad",1);
 for(unsigned m=0;m<4;++m)g.memories.push_back({"ram"+std::to_string(m),96,8,{}});
 // Shared enables, mixed memories, overlapping addresses and later groups
 // writing over earlier ones. Chunk size 7 also splits shared-enable groups.
 for(unsigned i=0;i<96;++i) {
  auto value=data;
  auto low=g.binary("add",slice(data,0,32),constant(i,32),32);
  std::copy(low.begin(),low.end(),value.begin());
  g.memoryWrites.push_back({(i/8)%4,address,value,slice(enable,(i/16)%3,1),0,bool((i/8)%4>=2)});
 }
 for(unsigned m=0;m<4;++m) {
  auto read=g.add("memory_read",64,address,constant(m,64),constant(0,64));
  auto high=g.add("memory_read",32,address,constant(m,64),constant(1,64));
  Value all=read;all.insert(all.end(),high.begin(),high.end());
  g.ports.push_back({"out"+std::to_string(m),all,false});
 }
 // Independent rejecting port: another RAM may already have prepared new
 // payloads whose inputs remain unchanged through disabled recovery clocks.
 g.memories.push_back({"reject",32,1,{}});
 g.memoryWrites.push_back({4,bad,constant(7,32),bad,0,false});
 // A state update must also roll back if any RAM address is rejected.
 auto count=g.add("state",32,constant(0,32),{},{},"counter");
 g.states.push_back({count,g.binary("add",count,constant(1,32),32),{1},0,false,{},{}});
 g.ports.push_back({"counter",count,false});
 g.attributes.push_back({"","native.owned_memories","1"});
 g.attributes.push_back({"","native.parallel_roots","1"});
 g.attributes.push_back({"","native.guard_chunk_calls","1"});
 g.attributes.push_back({"","native.cache_small_memory_commits","1"});
 g.attributes.push_back({"","native.cache_memory_commits","1"});
 g.attributes.push_back({"","native.parallel_memory_commits","1"});
 if(std::string(argv[3])!="old")g.attributes.push_back({"","native.cache_simple_transactions","1"});
 if(std::string(argv[3])=="old") {
  g.attributes.push_back({"","native.ungrouped_transactions","1"});
  g.attributes.push_back({"","native.unconditional_memory_writes","1"});
  g.attributes.push_back({"","native.uncached_transactions","1"});
 }
 g.emit(argv[1],7,std::stoul(argv[2]),false,true,true,false,true,std::stoul(argv[4]));
}
''')
(w/'run.cpp').write_text(r'''
#include "model.h"
#include <array>
#include <cassert>
#include <cstdio>
int main() {
 cpphdl_native::Model g;
 std::array<std::array<std::array<uint64_t,2>,8>,4> expected{};
 bool previous=false;unsigned counter=0;
 auto check=[&] {
  for(unsigned m=0;m<4;++m) {
   const auto& actual=m==0?g.memory0:m==1?g.memory1:m==2?g.memory2:g.memory3;
   const auto& out=m==0?g.out0:m==1?g.out1:m==2?g.out2:g.out3;
   for(unsigned row=0;row<8;++row)assert(actual[row]==expected[m][row]);
   const auto& row=expected[m][g.address[0]];
   assert(out[0]==uint32_t(row[0])&&out[1]==uint32_t(row[0]>>32)&&out[2]==row[1]);
  }
  assert(g.counter[0]==counter);
 };
 for(unsigned t=0;t<384;++t) {
  g.clock=(t/3)%2;g.address[0]=(t/11)%8;
  g.data={t*99991u,t*71717u,t*13131u};g.enable[0]=(t/5)%8;
  // Disabled invalid writes are legal; read outputs are not observed here.
  if(t%47==0) {
   bool level=g.clock;g.clock=previous;g.address[0]=15;g.enable[0]=0;g.eval(true);
   g.address[0]=(t/11)%8;g.clock=level;
  }
  // Rejected enabled writes cannot commit earlier valid ports or the counter.
  if(t%31==0&&g.clock!=previous) {
   auto address=g.address[0];g.address[0]=15;g.enable[0]=7;
   for(unsigned retry=0;retry<2;++retry)
    try{g.eval(true);assert(false);}catch(const std::out_of_range&){}
   g.address[0]=address;g.eval(false);check();
  }
  if(g.clock!=previous) {
   for(unsigned i=0;i<96;++i)if(bool((i/8)%4>=2)==!g.clock&&(g.enable[0]&(1u<<((i/16)%3))))
    expected[(i/8)%4][g.address[0]]={uint32_t(g.data[0]+i)|(uint64_t(g.data[1])<<32),g.data[2]};
   if(g.clock)++counter;
  }
  previous=g.clock;g.step();check();g.eval(false);check();
  if(t%53==0){auto copy=g;copy.eval(false);g=copy;check();}
 }
 // Repeat exactly the same write data on alternating edges; changed-word
 // suppression must not alter last-write-wins behavior between groups.
 for(unsigned t=0;t<128;++t) {
  g.clock=!previous;g.address[0]=0;g.enable[0]=7;
  for(unsigned i=0;i<96;++i)if(bool((i/8)%4>=2)==!g.clock)
   expected[(i/8)%4][0]={uint32_t(g.data[0]+i)|(uint64_t(g.data[1])<<32),g.data[2]};
  if(g.clock)++counter;previous=g.clock;g.step();check();
 }
 // Prepared payload changes from a rejected transaction must survive a
 // successful ALL-DISABLED recovery, even though the next retry is a cache hit.
 g.enable[0]=0;g.clock=false;g.step();previous=false;check();
 g.address[0]=0;g.data={12345,67890,98765};g.enable[0]=7;g.bad[0]=1;g.clock=!previous;
 try{g.step();assert(false);}catch(const std::out_of_range&){}
 {auto copy=g;g=copy;}
 g.bad[0]=0;g.enable[0]=0;g.step();if(g.clock)++counter;previous=g.clock;check();
 g.clock=!previous;g.step();if(g.clock)++counter;previous=g.clock;check();
 g.clock=!previous;g.enable[0]=7;
 for(unsigned i=0;i<96;++i)if(bool((i/8)%4>=2)==!g.clock)
  expected[(i/8)%4][0]={uint32_t(g.data[0]+i)|(uint64_t(g.data[1])<<32),g.data[2]};
 if(g.clock)++counter;previous=g.clock;g.step();check();
 // Explicit debug invalidation must also invalidate a previously idempotent
 // write set, so the next identical active transaction restores the row.
 g.memory0[0][0]^=255;expected[0][0][0]^=255;g.__cpphdl_invalidate_memories();
 g.enable[0]=0;g.clock=false;g.step();previous=false;check();
 g.enable[0]=7;g.clock=true;
 for(unsigned i=0;i<96;++i)if((i/8)%4<2)
  expected[(i/8)%4][0]={uint32_t(g.data[0]+i)|(uint64_t(g.data[1])<<32),g.data[2]};
 ++counter;previous=true;g.step();check();
 puts("PASS grouped memory: shared enables, wide data, split commits, priority, clocks, copies, rejection, inactive recovery, debug invalidation");
}
''')
flags = [a.cxx, '-std=c++23', '-O1', '-pthread', '-I'+str(include)]
subprocess.run([*flags,w/'emit.cpp','-o',w/'emit'],check=True)
for name, lanes, split, kind in [('old',6,1,'old'),('serial',1,0,'new'),('inline',6,0,'new'),('split',6,1,'new')]:
    d=w/name;d.mkdir(exist_ok=True)
    subprocess.run([w/'emit',d/'model.h',str(lanes),kind,str(split)],check=True)
    generated=(d/'model.h').read_text()
    if lanes>1:
        groups=int(re.search(r'__cpphdl_write_groups = (\d+)',generated).group(1))
        assert groups==97 if kind=='old' else groups<97
    assert ('__cpphdl_changed_memories' in generated)==(lanes>1)
    commits_lanes=int(re.search(r'__cpphdl_memory_commit_lanes = (\d+)',generated).group(1))
    assert (commits_lanes>1)==(lanes>1)
    if lanes>1:
        assert ('__cpphdl_cached_transactions = true' in generated)==(kind!='old')
        commits=int(re.search(r'__cpphdl_cached_memory_commits = (\d+)',generated).group(1))
        assert (commits>0)==(kind!='old')
    subprocess.run([*flags,'-I'+str(d),w/'run.cpp',*sorted(d.glob('model.part.*.cpp')),'-o',d/'run'],check=True)
    for mode in ('serial','parallel','auto'):
        subprocess.run([d/'run'],env=dict(os.environ,CPPHDL_NATIVE_DISPATCH=mode),check=True,timeout=60)
