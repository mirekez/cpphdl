"""Check memoized pure native chunks against uncached evaluation."""
import argparse
import re
from pathlib import Path
import subprocess
import tempfile

p=argparse.ArgumentParser();p.add_argument('--cxx',required=True);a=p.parse_args()
root=Path(__file__).resolve().parents[2]
work=Path(tempfile.mkdtemp(prefix='cpphdl-native-memoization-'))
def run(args):
    result=subprocess.run(list(map(str,args)),text=True,capture_output=True,timeout=240)
    if result.returncode:raise RuntimeError(result.stdout+result.stderr)
    return result.stdout

(work/'emit.cpp').write_text(r'''
#include <cpphdl_graph_native.h>
#include <cassert>
using namespace cpphdl::graph;
int main(int argc,char**argv) {
 Graph g;g.clockContract=ClockContract::NamedEdges;g.clocks.push_back({"clock",100});
 auto input=[&](const char* n,unsigned width){auto v=g.wire(width,n,"input");g.ports.push_back({n,v,true});return v;};
 auto data=input("data",64),enable=input("enable",1),reset=input("reset",1),address=input("address",2);
 g.memories.push_back({"rom",64,4,{11,23,37,59}});
 auto rom=g.add("memory_read",64,address,constant(0,64),constant(0,64));
 auto fixed=g.add("state",64,constant(0xa735800012345678ull,64),{}, {},"fixed");
 g.ports.push_back({"fixed",fixed,false});
 g.pipelines.push_back({"retained_metadata"});
 for(unsigned lane=0;lane<12;++lane) {
  auto state=g.wire(64,"state"+std::to_string(lane),"state");auto value=g.binary("xor",data,state,64);
  // Model the obsolete per-bit nodes left after register-word packing.
  for(unsigned bit=0;bit<64;++bit){auto alias=g.add("state",1,constant(0,1),{}, {},"old");g.connect(alias,{state[bit]});}
  for(unsigned i=0;i<350;++i)value=g.binary("xor",g.binary("add",value,rom,64),constant(i+lane*101,64),64);
  auto next=g.mux(enable,value,state);
  if(lane&2) {
   next.clear();
   for(unsigned bit=0;bit<64;++bit)
    next.push_back(g.mux(enable,Value{value[(bit*13+7)%64]},Value{state[bit]})[0]);
  }
  g.states.push_back({state,next,{1},0,bool(lane&1),reset,constant(lane,64)});
  g.ports.push_back({"out"+std::to_string(lane),value,false});
  g.ports.push_back({"state_out"+std::to_string(lane),state,false});
 }
 for(unsigned invalid=0;invalid<2;++invalid) {
  auto bad=g;if(invalid)bad.memories[0].contents.clear();else bad.add("host_random",32);
  bool rejected=false;try{bad.emit(std::string(argv[1])+"bad.h",7,6,false,false,true);}catch(const std::runtime_error&){rejected=true;}
  assert(rejected);
 }
 g.emit(std::string(argv[1])+"plain.h",7,6);
 bool rejected=false;try{g.emit(std::string(argv[1])+"invalid.h",7,6,false,false,false,true);}catch(const std::runtime_error&){rejected=true;}
 assert(rejected);
 g.emit(std::string(argv[1])+"cached_only.h",7,6,false,false,true);
 g.emit(std::string(argv[1])+"cached.h",7,6,false,false,true,true);
 g.emit(std::string(argv[1])+"cached_one.h",19,1,false,false,true,true);
}
''')
run([a.cxx,'-std=c++23','-O1','-I'+str(root/'include'),work/'emit.cpp','-o',work/'emit'])
run([work/'emit',str(work)+'/'])
for name in ('plain.h','cached_only.h','cached.h','cached_one.h'):
    fields=re.findall(r'uint(?:8|16|32|64)_t state\d+ =', (work/name).read_text())
    if len(fields)!=13:raise RuntimeError(f'{name}: expected 12 written and one fixed live state word, got {len(fields)}')
prefix='''#define cpphdl_native plain
#include "plain.h"
#undef cpphdl_native
#define cpphdl_native cached
#include "cached.h"
#undef cpphdl_native
#define cpphdl_native cached_one
#include "cached_one.h"
#undef cpphdl_native
#define cpphdl_native cached_only
#include "cached_only.h"
#undef cpphdl_native
#include <cassert>
#include <cstdio>
template<class M>void inputs(M& m,unsigned i,uint64_t data){m.data={uint32_t(data),uint32_t(data>>32)};m.enable[0]=(i/73)%2;m.reset[0]=i%257==0;m.address[0]=(i/17)%4;m.clock=(i/3)%2;}
template<class M>bool same(const plain::Model& a,const M& b){return '''
checks=['a.'+name+'==b.'+name for i in range(12) for name in (f'out{i}',f'state_out{i}')]
checks+=['a.fixed==b.fixed','a.fixed[0]==0x12345678u','a.fixed[1]==0xa7358000u']
(work/'check.cpp').write_text(prefix+' && '.join(checks)+r''';}
int main(){plain::Model a;cached::Model b;cached_one::Model c;cached_only::Model d;uint64_t random=42;bool serial=false,parallel=false;
 static_assert(plain::Model::__cpphdl_cached_next_words==0);
 static_assert(cached_only::Model::__cpphdl_cached_next_words==0);
 static_assert(cached::Model::__cpphdl_cached_next_words>0&&cached_one::Model::__cpphdl_cached_next_words>0);
 for(unsigned i=0;i<4096;++i){if(i%101==0){random^=random<<13;random^=random>>7;random^=random<<17;}
  inputs(a,i,random);inputs(b,i,random);inputs(c,i,random);inputs(d,i,random);
  for(bool commit:{false,true,false}){a.eval(commit);b.eval(commit);c.eval(commit);d.eval(commit);assert(same(a,b)&&same(a,c)&&same(a,d));serial|=b.__cpphdl_serial_dispatch;parallel|=!b.__cpphdl_serial_dispatch;}
 }
 assert(serial&&parallel);
 auto copied=b;auto reference=a;
 for(unsigned i=0;i<256;++i){inputs(copied,i,7919*i);inputs(reference,i,7919*i);copied.step();reference.step();assert(same(reference,copied));}
 std::puts("PASS native chunk memoization: 12288 phases, six/one lanes, adaptive serial/parallel dispatch, both edges, reset, ROM, copied state and rejection checks");
}
''')
run([a.cxx,'-std=c++23','-O1','-pthread','-fsanitize=undefined','-fno-sanitize-recover=all',
     '-I'+str(root/'include'),work/'check.cpp','-o',work/'check'])
print(run([work/'check']),end='')
