"""Automatically grouped scopes and independent tasks preserve graph semantics.

--build-only creates the same fixture for optional external throughput timing.
"""
import argparse
import os
import shutil
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
shutil.copyfile(Path(__file__).with_name('NativeLocalTasksEmit.cc'), w/'emit.cpp')
shutil.copyfile(Path(__file__).with_name('NativeLocalTasksRun.cc'), w/'run.cpp')
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
