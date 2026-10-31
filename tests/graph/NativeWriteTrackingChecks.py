"""Compare commit notifications with full scans, including rollback and copies."""
import argparse
import os
from pathlib import Path
import subprocess
import tempfile

p = argparse.ArgumentParser(description=__doc__)
p.add_argument('--cxx', default='g++')
a = p.parse_args()
include = Path(__file__).resolve().parents[2]/'include'

with tempfile.TemporaryDirectory(prefix='cpphdl-write-tracking-') as tmp:
    w = Path(tmp)
    (w/'emit.cpp').write_text(r'''
#include "cpphdl_graph.h"
using namespace cpphdl::graph;
int main(int argc,char**argv) {
 Graph g;g.clockContract=ClockContract::NamedEdges;g.clocks.push_back({"clock",100});
 auto input=[&](const char*n,unsigned width){auto v=g.wire(width,n,"input");g.ports.push_back({n,v,true});return v;};
 auto data=input("data",64),enable=input("enable",2),reset=input("reset",1),address=input("address",4);
 auto state=g.add("state",64,constant(0x123456789abcdef0ull,64),{},{},"shared");
 auto first=g.mux(slice(enable,0,1),data,state);
 // Select the original word for an idle cycle. Named-edge graphs have one
 // update per register; ordered writes are covered by RuntimeSplitChecks.
 g.states.push_back({state,g.mux(slice(enable,1,1),state,first),{1},0,false,reset,constant(7,64)});
 g.ports.push_back({"state_out",state,false});
 for(unsigned i=0;i<12;++i) {
  auto s=g.add("state",32,constant(i+11,32),{},{},"lane"+std::to_string(i));
  auto value=g.binary("xor",slice(state,(i&1)*32,32),s,32);
  for(unsigned j=0;j<24;++j)value=g.binary("add",g.binary("mul",value,constant(3,32),32),constant(j+i,32),32);
  g.states.push_back({s,g.mux(slice(enable,0,1),value,s),{1},0,bool(i&1),reset,constant(i,32)});
  g.ports.push_back({"out"+std::to_string(i),g.binary("xor",s,slice(state,(i&1)*32,32),32),false});
 }
 g.memories.push_back({"ram",64,8,{}});
 g.memoryWrites.push_back({0,address,data,slice(enable,0,1),0,false});
 g.memoryAccesses.push_back({0,address,slice(enable,0,1),true,0,false});
 g.attributes.push_back({"","native.owned_memories","1"});
 g.attributes.push_back({"","native.parallel_roots","1"});
 g.attributes.push_back({"","native.guard_chunk_calls","1"});
 auto scan=g;scan.attributes.push_back({"","native.scan_roots","1"});
 scan.emit(std::string(argv[1])+"scan.h",16,6,false,true,true,false,true);
 auto one=g;one.emit(std::string(argv[1])+"one.h",16,1,false,true,true,false,true);
 g.emit(std::string(argv[1])+"many.h",16,6,false,true,true,false,true);
}
''')
    (w/'run.cpp').write_text(r'''
#define cpphdl_native reference
#include "scan.h"
#undef cpphdl_native
#define cpphdl_native scalar
#include "one.h"
#undef cpphdl_native
#include "many.h"
#include <cassert>
#include <cstdio>
template<class A,class B> void equal(const A&a,const B&b) {
 assert(a.state_out==b.state_out && a.memory0==b.memory0);
 assert(a.out0==b.out0 && a.out1==b.out1 && a.out2==b.out2 && a.out3==b.out3);
 assert(a.out4==b.out4 && a.out5==b.out5 && a.out6==b.out6 && a.out7==b.out7);
 assert(a.out8==b.out8 && a.out9==b.out9 && a.out10==b.out10 && a.out11==b.out11);
}
template<class M> void drive(M&m,unsigned t,bool setters) {
 m.clock=(t&1);m.reset[0]=(t%109<3);m.address[0]=t%8;
 if constexpr(M::__cpphdl_write_tracking) {
  if(setters) {m.__cpphdl_set_data(0,t*117);m.__cpphdl_set_data(1,t*947);m.__cpphdl_set_enable(0,(t/7)%4);return;}
 }
 m.data[0]=t*117;m.data[1]=t*947;m.enable[0]=(t/7)%4;
}
template<class M> void reject(M&m) {
 m.clock=false;m.reset[0]=0;m.enable[0]=0;m.eval(true);m.eval(false);
 m.address[0]=15;m.enable[0]=1;m.clock=true;
 try {m.eval(true);assert(false);}catch(const std::out_of_range&){}
 m.address[0]=0;m.enable[0]=0;m.eval(false);
}
int main() {
 static_assert(cpphdl_native::Model::__cpphdl_write_tracking);
 static_assert(scalar::Model::__cpphdl_write_tracking);
 static_assert(!reference::Model::__cpphdl_write_tracking);
 static_assert(cpphdl_native::Model::__cpphdl_input_root_count<16);
 reference::Model a;scalar::Model b;cpphdl_native::Model c;
 for(unsigned t=0;t<1024;++t) {
  drive(a,t,false);drive(b,t,t%3);drive(c,t,t%2);
  a.step();b.step();c.step();equal(a,b);equal(a,c);
  // Repeat output-only evaluations and copy pending dirty/cache state.
  for(unsigned j=0;j<3;++j){a.eval(false);b.eval(false);c.eval(false);equal(a,b);equal(a,c);}
  if(t%71==0){auto clone=c;clone.eval(false);equal(c,clone);c=clone;}
 }
 reject(a);reject(b);reject(c);equal(a,b);equal(a,c);
 for(unsigned t=1024;t<1056;++t){drive(a,t,false);drive(b,t,true);drive(c,t,true);a.step();b.step();c.step();equal(a,b);equal(a,c);}
 try{c.__cpphdl_set_data(2,9);assert(false);}catch(const std::out_of_range&){}
 puts("PASS write tracking: raw/setter inputs, shared roots, clocks/reset, copies, failed commits");
}
''')
    flags = [a.cxx, '-std=c++23', '-O1', '-pthread', '-I'+str(include)]
    subprocess.run([*flags, w/'emit.cpp', '-o', w/'emit'], check=True)
    subprocess.run([w/'emit', str(w)+'/'], check=True)
    for name in ('one', 'many'):
        text = (w/(name+'.h')).read_text()
        assert '__cpphdl_root_keys' not in text and '__cpphdl_roots_lane' not in text
    subprocess.run([*flags, w/'run.cpp', '-o', w/'run'], check=True)
    for mode in ('serial', 'parallel', 'auto'):
        subprocess.run([w/'run'], env=dict(os.environ, CPPHDL_NATIVE_DISPATCH=mode), check=True, timeout=90)
