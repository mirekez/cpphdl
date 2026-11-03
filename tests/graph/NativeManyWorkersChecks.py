"""Thirty workers and 120 generated tasks preserve cross-task old-state reads.

This is a correctness test; it does not require 30 physical cores or measure
scaling. Each dispatch policy must agree with the independent scalar oracle.
"""
import argparse
import os
from pathlib import Path
import subprocess
import tempfile

p = argparse.ArgumentParser(description=__doc__)
p.add_argument('--cxx', default='g++')
a = p.parse_args()
include = Path(__file__).resolve().parents[2]/'include'
with tempfile.TemporaryDirectory(prefix='native-many-workers-') as tmp:
    w = Path(tmp)
    (w/'emit.cpp').write_text(r'''
#include "cpphdl_graph.h"
using namespace cpphdl::graph;
int main(int argc,char**argv) {
 Graph g;g.clockContract=ClockContract::RisingEdgeStep;
 auto data=g.wire(32,"data","input");g.ports.push_back({"data",data,true});
 std::vector<Value> state;Value output;
 for(unsigned i=0;i<120;++i) {
  g.currentScope="unit"+std::to_string(i);
  state.push_back(g.add("state",32,constant(0,32),{},{},"r"));
  output.insert(output.end(),state.back().begin(),state.back().end());
 }
 for(unsigned i=0;i<120;++i) {
  g.currentScope="unit"+std::to_string(i);
  auto next=g.binary("add",state[(i+1)%120],data,32);
  next=g.binary("xor",next,constant(i,32),32);
  g.states.push_back({state[i],next,{1}});
 }
 g.currentScope.clear();g.ports.push_back({"result",output,false});
 g.attributes.push_back({"","native.tasks","120"});
 g.attributes.push_back({"","native.scope_tasks","1"});
 g.emit(argv[1],64,30,false,true,true,false,true);
}
''')
    (w/'run.cpp').write_text(r'''
#include "model.h"
#include <array>
#include <cassert>
#include <cstdio>
int main() {
 using Model=cpphdl_native::Model;
 static_assert(Model::__cpphdl_thread_count==30);
 static_assert(Model::__cpphdl_task_count==120);
 Model g;std::array<uint32_t,120> expected{};
 for(unsigned t=0;t<96;++t) {
  uint32_t data=t*1009;g.data[0]=data;auto old=expected;
  for(unsigned i=0;i<120;++i)expected[i]=(old[(i+1)%120]+data)^i;
  g.step();
  for(unsigned i=0;i<120;++i)
   assert(uint32_t(g.result[i])==expected[i]);
  if(t%17==0){auto copy=g;g=copy;g.eval(false);}
 }
 puts("PASS 30 workers / 120 tasks: cross-task snapshots and copies");
}
''')
    flags = [a.cxx, '-std=c++23', '-O1', '-pthread', '-I'+str(include)]
    subprocess.run([*flags, w/'emit.cpp', '-o', w/'emit'], check=True)
    subprocess.run([w/'emit', w/'model.h'], check=True)
    subprocess.run([*flags, w/'run.cpp', '-o', w/'run'], check=True)
    for policy in ('serial', 'parallel', 'auto'):
        subprocess.run([w/'run'], env=dict(os.environ, CPPHDL_NATIVE_DISPATCH=policy), check=True, timeout=60)
