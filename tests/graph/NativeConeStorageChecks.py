#!/usr/bin/env python3
"""Shared sink cones must not require a retained dependency list per sink."""
import argparse
from pathlib import Path
import resource
import subprocess
import tempfile

p = argparse.ArgumentParser(description=__doc__)
p.add_argument('--cxx', default='g++')
a = p.parse_args()

def memory_limit():
    resource.setrlimit(resource.RLIMIT_AS, (96 * 1024**2,) * 2)

with tempfile.TemporaryDirectory(prefix='cpphdl-native-cones-') as tmp:
    w = Path(tmp)
    (w/'check.cpp').write_text(r'''
#include "cpphdl_graph.h"
using namespace cpphdl::graph;
int main() {
 Graph g;
 auto input = g.wire(64, "input", "input");
 g.ports.push_back({"input", input, true});
 auto value = input;
 for (unsigned i=0; i<8192; ++i) value = g.binary("add", value, constant(i+1,64),64);
 for (unsigned i=0; i<2048; ++i) g.ports.push_back({"out"+std::to_string(i),value,false});
 auto order = g.dependencyOrder();
 auto plan = nativeParallelPlan(g,order,6,false,true);
 if (!plan.lanes || plan.lanes>6 || plan.portWords.size()!=g.ports.size()) return 1;
 for (size_t lane=0; lane<plan.tasks.size(); ++lane) {
  std::vector<bool> ready(g.nodes.size());
  for (auto index:plan.tasks[lane]) {
   const auto& n=g.nodes[index];
   for (auto* field:{&n.left,&n.right,&n.select}) for(auto raw:*field) {
    auto bit=g.resolve(raw); if(bit<2)continue;
    auto producer=Graph::owner(bit);
    if(g.nodes[producer].op!="input" && !ready[producer])return 2;
   }
   ready[index]=true;
  }
  for(size_t i=1;i<g.ports.size();++i)if(plan.portWords[i][0]==lane)
   for(auto bit:g.ports[i].bits)if(!ready[Graph::owner(bit)])return 3;
 }
 puts("PASS overlapping sink cones under 96 MiB address-space limit");
}
''')
    include=Path(__file__).resolve().parents[2]/'include'
    subprocess.run([a.cxx,'-std=c++23','-O1','-I'+str(include),str(w/'check.cpp'),'-o',str(w/'check')],check=True)
    subprocess.run([w/'check'],check=True,preexec_fn=memory_limit,timeout=120)
