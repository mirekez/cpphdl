"""Sparse native gathers: independent bit oracle, state/reset and worker lanes."""
import argparse
from pathlib import Path
import subprocess
import sys
import tempfile
sys.path.insert(0,str(Path(__file__).resolve().parents[1]))
from TestToolchain import TestToolchain
p=argparse.ArgumentParser();p.add_argument('--cxx',required=True);a=p.parse_args()
root=Path(__file__).resolve().parents[2]
with tempfile.TemporaryDirectory(prefix='cpphdl-bit-gather-') as temporary:
    work=Path(temporary);toolchain=TestToolchain(a.cxx,None,work)
    def run(command):
        r=subprocess.run(toolchain.command(command),env=toolchain.env,text=True,capture_output=True,timeout=240)
        if r.returncode:raise RuntimeError(str(command)+'\n'+r.stdout+r.stderr)
        return r.stdout
    (work/'routes.h').write_text(r'''
#pragma once
#include <cstdint>
inline unsigned route(unsigned word,unsigned bit){
 switch(word%10){
 case 0:return bit+(bit%2?64:0);
 case 1:return bit%3?127:191;
 case 2:return bit%32;
 case 3:return 63;
 case 4:return 63-bit;
 case 5:return bit<32?bit+31:192+(bit%2);
 case 6:return (word*71+bit*37+bit*bit)%194;
 case 7:return bit==0?63:bit==63?0:193;
 case 8:return (bit%7)*31%192;
 default:return bit<32?bit*2:192;
 }
}
''')
    (work/'emit.cpp').write_text(r'''
#include <cpphdl_graph_native.h>
#include "routes.h"
using namespace cpphdl::graph;
int main(int argc,char**argv){
 Graph g;g.clockContract=ClockContract::RisingEdgeStep;
 auto data=g.wire(192,"data","input"),enable=g.wire(1,"enable","input"),reset=g.wire(1,"reset","input");
 g.ports.push_back({"data",data,true});g.ports.push_back({"enable",enable,true});g.ports.push_back({"reset",reset,true});
 Value gathered,stateOut;
 for(unsigned word=0;word<40;++word){
  Value bits;for(unsigned bit=0;bit<64;++bit){auto source=route(word,bit);bits.push_back(source<192?data[source]:source-192);}
  gathered.insert(gathered.end(),bits.begin(),bits.end());
  auto state=g.add("state",64,constant(0x8735000000000000ull+word,64));
  auto next=g.binary("xor",bits,constant(0xabc0123456789defull+word,64),64);
  g.states.push_back({state,g.mux(reset,constant(word+17,64),g.mux(enable,next,state)),{1}});
  stateOut.insert(stateOut.end(),state.begin(),state.end());
 }
 g.ports.push_back({"gathered",gathered,false});g.ports.push_back({"state_out",stateOut,false});
 {auto copy=g;copy.emit(std::string(argv[1])+"plain.h");}
 g.attributes.push_back({"","native.coalesce_bits","1"});
 {auto copy=g;copy.emit(std::string(argv[1])+"coalesced.h");}
 {auto copy=g;copy.emit(std::string(argv[1])+"parallel.h",7,6,false,false,true,false,true);}
 g.attributes.push_back({"","native.bmi2_gathers","1"});
 g.emit(std::string(argv[1])+"bmi.h",7,6,false,false,true,false,true);
}
''')
    flags=[a.cxx,'-std=c++23','-O1','-I'+str(root/'include')]
    run([*flags,work/'emit.cpp','-o',work/'emit']);run([work/'emit',str(work)+'/'])
    before=(work/'plain.h').stat().st_size;after=(work/'coalesced.h').stat().st_size
    if after>=before:raise RuntimeError('Gather code-size reduction not exercised')
    if 'graph_runtime::gatherBits(' not in (work/'bmi.h').read_text():raise RuntimeError('BMI2 gather not exercised')
    (work/'check.cpp').write_text(r'''
#define cpphdl_native plain
#include "plain.h"
#undef cpphdl_native
#define cpphdl_native coalesced
#include "coalesced.h"
#undef cpphdl_native
#define cpphdl_native parallel
#include "parallel.h"
#undef cpphdl_native
#define cpphdl_native bmi
#include "bmi.h"
#undef cpphdl_native
#include "routes.h"
#include <cassert>
#include <cstdio>
#include <random>
int main(){plain::Model a;coalesced::Model b;parallel::Model c;bmi::Model d;std::mt19937_64 rng(731);
 for(unsigned i=0;i<20000;++i){auto v=rng(),source=rng(),destination=rng();if(i<64){source=uint64_t(1)<<i;destination=uint64_t(1)<<(63-i);}
  if(i==64)source=destination=0;if(i==65)source=destination=~uint64_t(0);
  uint64_t expected=0;unsigned from=0;for(unsigned to=0;to<64;++to)if((destination>>to)&1){
   while(from<64&&!((source>>from)&1))++from;if(from<64)expected|=((v>>from++)&1)<<to;}
  assert(cpphdl::graph_runtime::gatherBitsPortable(v,source,destination)==expected);
  assert(cpphdl::graph_runtime::gatherBits(v,source,destination)==expected);
 }
 std::array<uint64_t,40> state{};for(unsigned w=0;w<40;++w)state[w]=0x8735000000000000ull+w;
 for(unsigned i=0;i<4096;++i){
  std::array<uint64_t,3> data{rng(),rng(),rng()};
  if(i<192){data={0,0,0};data[i/64]=uint64_t(1)<<(i%64);}
  const bool enable=i%7!=0,reset=i%193==0;
  auto inputs=[&](auto& m){m.enable[0]=enable;m.reset[0]=reset;for(unsigned w=0;w<6;++w)m.data[w]=uint32_t(data[w/2]>>(32*(w%2)));};
  inputs(a);inputs(b);inputs(c);inputs(d);
  d.__cpphdl_dispatch_policy=c.__cpphdl_dispatch_policy=i%2?cpphdl::graph_runtime::DispatchPolicy::Parallel:cpphdl::graph_runtime::DispatchPolicy::Serial;
  for(bool commit:{false,true,false}){
   a.eval(commit);b.eval(commit);c.eval(commit);d.eval(commit);
   for(unsigned w=0;w<40;++w){uint64_t expected=0;
    for(unsigned bit=0;bit<64;++bit){auto source=route(w,bit);auto value=source<192?(data[source/64]>>(source%64))&1:source-192;expected|=value<<bit;}
    auto check=[&](auto& m){assert((uint64_t(m.gathered[2*w])|(uint64_t(m.gathered[2*w+1])<<32))==expected);
     assert((uint64_t(m.state_out[2*w])|(uint64_t(m.state_out[2*w+1])<<32))==state[w]);};
    check(a);check(b);check(c);check(d);
    if(commit){if(reset)state[w]=w+17;else if(enable)state[w]=expected^(0xabc0123456789defull+w);}
   }
  }
 }
 std::puts("PASS native coalesced gathers: 12288 phases, sparse masks, repeated bits, shifts 0/63, constants, reset, portable/BMI2 and serial/six-lane oracle");
}
''')
    run([*flags,'-O2','-pthread','-fsanitize=undefined','-fno-sanitize-recover=all',work/'check.cpp','-o',work/'check'])
    print(run([work/'check']),end='');print(f'Unchunked model text: {before} -> {after} bytes')
    (work/'cpu.cpp').write_text('''
#include <cstdio>
int main(){
#if defined(__x86_64__) && (defined(__GNUC__) || defined(__clang__))
 if(__builtin_cpu_supports("bmi2"))std::puts("BMI2");
#endif
}
''')
    run([*flags,work/'cpu.cpp','-o',work/'cpu'])
    if 'BMI2' in run([work/'cpu']):
        run([*flags,'-O2','-mbmi2','-pthread','-fsanitize=undefined','-fno-sanitize-recover=all',work/'check.cpp','-o',work/'check-inline'])
        print('Explicit BMI2 target: '+run([work/'check-inline']),end='')
