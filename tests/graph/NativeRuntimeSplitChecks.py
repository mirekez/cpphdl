#!/usr/bin/env python3
"""Check ordered RAM commits across generated native compilation units."""
import argparse
import os
from pathlib import Path
import resource
import subprocess
import tempfile

p = argparse.ArgumentParser(description=__doc__)
p.add_argument('--cxx', default='g++')
p.add_argument('--include', type=Path)
a = p.parse_args()
original = Path(__file__).resolve().parents[2]/'include'
include = a.include or original

def limit():
    resource.setrlimit(resource.RLIMIT_AS, (4*1024**3,)*2)

def run(command, **kwargs):
    subprocess.run(command, check=True, preexec_fn=limit, **kwargs)

with tempfile.TemporaryDirectory(prefix='cpphdl-native-runtime-split-') as tmp:
    w = Path(tmp)
    (w/'emit.cpp').write_text(r'''
#include "cpphdl_graph.h"
using namespace cpphdl::graph;
int main(int argc,char**argv){
 Graph g;g.clockContract=ClockContract::RisingEdgeStep;
 auto input=[&](const char*n,unsigned width){auto v=g.wire(width,n,"input");g.ports.push_back({n,v,true});return v;};
 auto address=input("address",4), data=input("data",32), enable=input("enable",9);
 g.memories.push_back({"ram",32,8,{}});
 for(unsigned i=0;i<9;++i)
  g.memoryWrites.push_back({0,address,g.binary("add",data,constant(i,32),32),slice(enable,i,1),-1,false});
 g.memoryAccesses.push_back({0,address,{1},false,-1,false});
 auto read=g.add("memory_read",32,address,constant(0,64),constant(0,64));
 g.ports.push_back({"result",read,false});
 auto counter=g.add("state",32,constant(0,32),{},{},"counter");
 g.states.push_back({counter,g.binary("add",counter,constant(1,32),32),{1},-1,false,{},{}});
 g.states.push_back({counter,g.binary("add",counter,constant(3,32),32),slice(enable,0,1),-1,false,{},{}});
 g.states.push_back({counter,counter,slice(enable,8,1),-1,false,{},{}});
 g.ports.push_back({"counter",counter,false});
 Value bank;
 for(unsigned i=0;i<16;++i) {
  auto word=g.add("state",32,constant(i,32),{},{},"bank"+std::to_string(i));
  g.states.push_back({word,data,slice(enable,i%9,1),-1,false,{},{}});
  bank.insert(bank.end(),word.begin(),word.end());
 }
 g.ports.push_back({"bank",bank,false});
 g.attributes.push_back({"","native.owned_memories","1"});
 g.attributes.push_back({"","native.cache_simple_transactions","1"});
 g.attributes.push_back({"","native.cache_small_memory_commits","1"});
 g.attributes.push_back({"","native.cache_memory_commits","1"});
 g.emit(argv[1],2,3,false,true,true,false,true,1);
 std::ofstream(std::string(argv[1])+".state.h") << "#define CPPHDL_TEST_BANK0 state" << Graph::owner(g.ports.back().bits[0]) << "\n";
}
''')
    (w/'run.cpp').write_text(r'''
#include "model.h"
#include "model.h.state.h"
#include <array>
#include <cstdio>
int main(){
 cpphdl_native::Model g;std::array<uint32_t,8> memory{};unsigned counter=0;
 std::array<uint32_t,16> bank{};for(unsigned i=0;i<16;++i)bank[i]=i;
 for(unsigned t=0;t<80;++t){
  const unsigned address=t%8, mask=t%11==0?0:((t*73)^511)&511;
  g.address[0]=address;g.data[0]=t*101;g.enable[0]=mask;
  for(unsigned i=0;i<9;++i)if(mask&(1u<<i))memory[address]=t*101+i;
  if(!(mask&256))counter += mask&1 ? 3 : 1;
  for(unsigned i=0;i<16;++i)if(mask&(1u<<(i%9)))bank[i]=t*101;
  g.step();
  if(g.result[0]!=memory[address]||g.counter[0]!=counter||g.bank!=bank)return 1;
  for(unsigned i=0;i<8;++i)if(g.memory0[i][0]!=memory[i])return 2;
 }
 // A failing transaction must not commit any earlier write helper or state.
 g.address[0]=15;g.enable[0]=511;
 try{g.step();return 3;}catch(const std::out_of_range&){}
 for(unsigned i=0;i<8;++i)if(g.memory0[i][0]!=memory[i])return 4;
 g.address[0]=0;g.enable[0]=0;g.eval(false);
 if(g.counter[0]!=counter||g.result[0]!=memory[0])return 5;
 g.step();++counter;if(g.counter[0]!=counter||g.bank!=bank)return 6;
 // Cached state prepared by a failed transaction must retain notifications
 // across an unchanged retry. Other groups change their enables on recovery.
 g.address[0]=15;g.enable[0]=511;g.data[0]=777;
 for(unsigned retry=0;retry<2;++retry) {
  try{g.step();return 7;}catch(const std::out_of_range&){}
  if(retry==0){auto copy=g;g=copy;}
  g.address[0]=0;g.eval(false);g.address[0]=15;
  if(g.counter[0]!=counter||g.bank!=bank)return 8;
 }
 g.address[0]=0;g.enable[0]=1;g.step();counter+=3;
 for(unsigned i=0;i<16;++i)if(i%9==0)bank[i]=777;
 if(g.counter[0]!=counter||g.bank!=bank||g.memory0[0][0]!=777)return 9;
 g.enable[0]=0;
 for(unsigned idle=0;idle<32;++idle) {
  g.step();++counter;
  if(g.counter[0]!=counter||g.bank!=bank)return 10;
 }
 g.__cpphdl_state[g.__cpphdl_current].CPPHDL_TEST_BANK0=123456;
 bank[0]=123456;g.__cpphdl_invalidate_state();
 for(unsigned idle=0;idle<4;++idle) {
  g.step();++counter;
  if(g.counter[0]!=counter||g.bank!=bank)return 11;
 }
 puts("PASS split native runtime: write/state priority, disabled ports, cache invalidation, failed transaction");
}
''')
    flags = [a.cxx, '-std=c++23', '-O1', '-pthread', '-I'+str(include), '-I'+str(original)]
    run([*flags, w/'emit.cpp', '-o', w/'emit'])
    run([w/'emit', w/'model.h'])
    if not all(name in (w/'model.h').read_text() for name in ('__cpphdl_commit_ram_', '__cpphdl_sinks_0_part_', '__cpphdl_cached_transactions = true', '__cpphdl_cached_state_transactions = true', '__cpphdl_cached_memory_commits = 1')):
        raise RuntimeError('Test did not exercise split RAM commits and sinks')
    run([*flags, w/'run.cpp', *sorted(w.glob('model.part.*.cpp')), '-o', w/'run'])
    for mode in ('serial', 'parallel', 'auto'):
        run([w/'run'], env=dict(os.environ, CPPHDL_NATIVE_DISPATCH=mode), timeout=30)
