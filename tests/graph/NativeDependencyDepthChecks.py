#!/usr/bin/env python3
"""Dependency traversal must handle whole-design reduction depth without recursion."""
import argparse
from pathlib import Path
import subprocess
import tempfile

p=argparse.ArgumentParser()
p.add_argument('--cxx',default='g++')
a=p.parse_args()
with tempfile.TemporaryDirectory(prefix='cpphdl-dependency-depth-') as tmp:
    w=Path(tmp)
    (w/'check.cpp').write_text(r'''
#include "cpphdl_graph.h"
using namespace cpphdl::graph;
int main(){
 Graph g;Value value{0};constexpr unsigned count=60000;
 for(unsigned i=0;i<count;++i){auto input=g.wire(1,"in"+std::to_string(i),"input");g.ports.push_back({"in"+std::to_string(i),input,true});value=g.binary("or",value,input,1);}
 g.ports.push_back({"out",value,false});auto order=g.dependencyOrder();
 if(order.size()!=2*count-1)return 1;
 std::vector<bool> seen(g.nodes.size());
 for(auto index:order){const auto& n=g.nodes[index];for(const auto* v:{&n.left,&n.right,&n.select})for(auto bit:*v){bit=g.resolve(bit);if(bit>1&&!seen[Graph::owner(bit)])return 2;}seen[index]=true;}
 Graph cycle;
 auto a=cycle.wire(1,"a"), b=cycle.wire(1,"b");
 // Independent inputs prevent the loop from folding into cancelling inversions.
 auto x=cycle.wire(1,"x","input"), y=cycle.wire(1,"y","input");
 cycle.ports.push_back({"x",x,true});
 cycle.ports.push_back({"y",y,true});
 cycle.connect(a,cycle.binary("xor",b,x,1));
 cycle.connect(b,cycle.binary("xor",a,y,1));
 cycle.ports.push_back({"out",a,false});
 try {
     cycle.dependencyOrder();
     return 3;
 } catch (const std::runtime_error& error) {
     if (std::string(error.what()).find("combinational cycle")==std::string::npos) {
         std::cerr<<error.what()<<'\n';
         return 4;
     }
 }
 std::cout<<"PASS deep dependency order and real-cycle rejection\n";
}
''')
    subprocess.run([a.cxx,'-std=c++23','-O1','-I'+str(Path(__file__).resolve().parents[2]/'include'),
                    str(w/'check.cpp'),'-o',str(w/'check')],check=True)
    subprocess.run([w/'check'],check=True,timeout=30)
