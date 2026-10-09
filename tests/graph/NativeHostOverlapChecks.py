"""Compare overlapped host transactions with synchronous graph/host execution."""
import argparse
from pathlib import Path
import subprocess
import re
import tempfile

p=argparse.ArgumentParser()
p.add_argument('--cxx',required=True)
a=p.parse_args()
root=Path(__file__).resolve().parents[2]
def run(argv):
    r=subprocess.run(list(map(str,argv)),capture_output=True,text=True,timeout=180)
    if r.returncode: raise RuntimeError(r.stdout+r.stderr)
    return r.stdout
with tempfile.TemporaryDirectory(prefix='cpphdl-host-overlap-') as tmp:
    w=Path(tmp)
    (w/'emit.cpp').write_text(r'''
#include <cpphdl_graph_native.h>
using namespace cpphdl::graph;
int main(int argc,char**argv) {
 Graph g;
 bool named=std::stoul(argv[4]);
 if(named){g.clockContract=ClockContract::NamedEdges;g.clocks.push_back({"clock",100});}
 auto input=[&](const char* name,unsigned width) {auto x=g.wire(width,name,"input");g.ports.push_back({name,x,true});return x;};
 auto data=input("data",64),address=input("address",8),enable=input("enable",1),reset=input("reset",1);
 g.memories.push_back({"ram",64,8});
 auto read=g.add("memory_read",64,address,constant(0,64),constant(0,64));
 g.memoryAccesses.push_back({0,address,enable,false});
 g.memoryWrites.push_back({0,address,data,enable});
 g.memoryWrites.push_back({0,address,read,g.binary("and",enable,slice(data,1,1),1)});
 constexpr unsigned widths[]={64,1,7,8,9,16,17,32,33,63,64};
 for(unsigned k=0;k<11;++k) {
  auto width=widths[k];
  auto state=g.wire(width,"state","state");
  auto next=g.binary("xor",state,data,64);
  for(unsigned i=0;i<39;++i)next=g.binary("add",g.binary("xor",next,constant(i*41+k,64),64),state,64);
  g.states.push_back({state,resize(g.binary("add",next,read,64),width),named?Value{1}:enable});
  if(named){auto&s=g.states.back();s.clock=0;s.falling=k%2;s.reset=reset;s.resetValue=constant(17+k,width);}
  else if(k==0)g.states.push_back({state,data,slice(data,0,1)});
  g.ports.push_back({"out"+std::to_string(k),resize(state,64),false});
 }
 g.ports.push_back({"read",read,false});
 if(named)for(auto&w:g.memoryWrites)w.clock=0;
 g.emit(argv[1],7,std::stoul(argv[2]),std::stoul(argv[3]));
}
''')
    run([a.cxx,'-std=c++23','-O1','-I'+str(root/'include'),w/'emit.cpp','-o',w/'emit'])
    for name,n,overlap in [('serial',1,0),('parallel',3,0),('overlap',3,1)]:
        for named in (0,1):
            header = w/(name+('_named' if named else '')+'.h')
            run([w/'emit',header,n,overlap,named])
            if name == 'serial':
                # Keep full-width reference storage to detect truncation or
                # promotion mistakes shared by the compact serial/parallel paths.
                header.write_text(re.sub(r'uint(?:8|16|32)_t (state[0-9]+)',
                                         r'uint64_t \1', header.read_text()))
    (w/'check.cpp').write_text(r'''
#define cpphdl_native serial
#include "serial.h"
#undef cpphdl_native
#define cpphdl_native parallel
#include "parallel.h"
#undef cpphdl_native
#include "overlap.h"
#define cpphdl_native serial_named
#include "serial_named.h"
#undef cpphdl_native
#define cpphdl_native overlap_named
#include "overlap_named.h"
#undef cpphdl_native
#include <cassert>
#include <cstdio>
#include <atomic>
#include <thread>
static_assert(cpphdl_native::Model::__cpphdl_thread_count==3);
static_assert(cpphdl_native::Model::__cpphdl_host_overlap);
template<class M> auto outputs(const M& m) {return std::array{m.out0,m.out1,m.out2,m.out3,m.out4,m.out5,m.out6,m.out7,m.out8,m.out9,m.out10,m.read};}
template<class M> void inputs(M& m,unsigned i) {
 m.data={i*1237u,0x80000000u+i*971u};m.address[0]=i%8;m.enable[0]=i%3!=0;m.reset[0]=i%97==0;
 if constexpr(requires{m.clock;})m.clock=(i/2)%2;
}
template<class R=serial::Model,class M> void check(M& m) {
 R ref;
 for(unsigned i=0;i<2048;++i) {
  inputs(m,i);inputs(ref,i);
  bool bad=i%19==0,hostThrows=i%17==0;
  if(bad) {m.address[0]=ref.address[0]=255;m.enable[0]=ref.enable[0]=1;}
  unsigned refCalls=0,calls=0;uint64_t refHost=0,host=0;
  auto tick=[&](const auto& x,unsigned& count,uint64_t& result) {
   ++count;for(auto port:outputs(x))result=result*131+port[0]+port[1];
   if(hostThrows)throw std::runtime_error("host exception");
  };
  unsigned refError=0,error=0;
  try {ref.template evaluate<true>();tick(ref,refCalls,refHost);}
  catch(const std::out_of_range&) {refError=1;}
  catch(const std::runtime_error&) {refError=2;}
  try {m.evaluate_with_host([&]{tick(m,calls,host);});}
  catch(const std::out_of_range&) {error=1;}
  catch(const std::runtime_error&) {error=2;}
  assert(error==refError && calls==refCalls && host==refHost);
  assert(outputs(m)==outputs(ref) && m.memory0==ref.memory0);
  if(bad)assert(calls==0);
  // Settle reveals committed registers, including after a host exception.
  m.address[0]=ref.address[0]=i%8;m.eval(false);ref.eval(false);
  assert(outputs(m)==outputs(ref) && m.memory0==ref.memory0);
  if(i%31==0) {auto copy=m;auto rcopy=ref;copy.step();rcopy.step();assert(outputs(copy)==outputs(rcopy));}
 }
}
struct Work {std::atomic<unsigned> started{0},done{0};std::atomic<bool> release{false};};
int main() {
 parallel::Model p;cpphdl_native::Model m;check(p);check(m);
 overlap_named::Model named;check<serial_named::Model>(named);
 // A barrier-driven handshake proves the continuation actually overlaps
 // workers, and throwing from it drains every worker before returning.
 cpphdl::graph_runtime::Threads executor(3,3);Work work;
 auto lane=[](void* ptr,auto&,unsigned lane,uint32_t) noexcept {
  if(!lane)return;auto& w=*static_cast<Work*>(ptr);w.started.fetch_add(1);
  while(!w.release.load())std::this_thread::yield();w.done.fetch_add(1);
 };
 bool threw=false;
 try {executor.run(&work,lane,[&] {
  while(work.started.load()!=2)std::this_thread::yield();
  assert(work.done.load()==0);work.release.store(true);throw std::runtime_error("host");
 });}catch(const std::runtime_error&){threw=true;}
 assert(threw && work.done.load()==2);
 executor.run(&work,lane);assert(work.done.load()==4);
 std::puts("host overlap: mixed-width registers, old-state outputs, RAM ordering, validation rollback, host exceptions, copies and worker drain PASS");
}
''')
    run([a.cxx,'-std=c++23','-O2','-pthread','-fsanitize=undefined','-fno-sanitize-recover=all',
         '-I'+str(root/'include'),w/'check.cpp','-o',w/'check'])
    print(run([w/'check']))
