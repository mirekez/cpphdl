#!/usr/bin/env python3
"""Check flattened instances, independent clocks and RAMs with native threads."""
import argparse
from pathlib import Path
import subprocess
import tempfile

p = argparse.ArgumentParser()
p.add_argument('--cxx', default='g++')
a = p.parse_args()
include = Path(__file__).resolve().parents[2]/'include'
with tempfile.TemporaryDirectory(prefix='cpphdl-native-link-') as tmp:
    w = Path(tmp)
    (w/'emit.cpp').write_text(r'''
#include "cpphdl_graph_link.h"
using namespace cpphdl::graph;
int main(int argc,char**argv){
 Graph c;c.clockContract=ClockContract::RisingEdgeStep;
 auto input=[&](const char*n,unsigned bits){auto v=c.wire(bits,n,"input");c.ports.push_back({n,v,true});return v;};
 auto data=input("data",8),enable=input("enable",1),reset=input("reset",1),address=input("address",2);
 auto state=c.add("state",8,constant(0,8),{}, {},"counter");
 auto sum=c.binary("add",state,data,8);
 c.states.push_back({state,c.mux(reset,constant(0,8),c.mux(enable,sum,state)),{1},-1,false,{},{}});
 c.ports.push_back({"q",state,false});
 c.memories.push_back({"ram",8,4,{}});
 c.memoryWrites.push_back({0,address,data,enable,-1,false});
 auto read=c.add("memory_read",8,address,constant(0,64),constant(0,64));
 c.ports.push_back({"r",read,false});
 explicitClockEvents(c);
 std::stringstream records;c.save(records);Graph restored;restored.load(records);c=std::move(restored);
 Graph g;
 for(unsigned i=0;i<2;++i){std::map<std::string,Value> pins;
  for(const auto&p:c.ports){auto n=p.name+std::to_string(i);auto v=g.wire(p.bits.size(),n,p.input?"input":"wire");g.ports.push_back({n,v,p.input});pins[p.name]=v;}
  linkGraphInstance(g,c,pins,"child"+std::to_string(i));
 }
 if(g.memories.size()!=2||g.clockContract!=ClockContract::ExplicitEvents)return 3;
 // Invalid bindings must fail before a partial instance is appended.
 auto before=g.nodes.size();try{linkGraphInstance(g,c,{},"bad");return 4;}catch(const std::runtime_error&){}
 if(g.nodes.size()!=before)return 5;
 g.compact();
 g.emit(argv[1],32,std::stoul(argv[2]),false,true,true,false,true);
}
''')
    (w/'run.cpp').write_text(r'''
#include "model.h"
#include <array>
#include <cstdio>
int main(){cpphdl_native::Model g;std::array<unsigned,2> q{};unsigned ram[2][4]{};bool previous[2]{};
 for(unsigned t=0;t<600;++t){unsigned levels[2]={(t/2)%2,(t/3)%2};
  unsigned data[2]={(t*13+1)%256,(t*7+21)%256},addr[2]={t%4,(t/2)%4};
  bool en[2]={t%5!=0,t%7!=0},reset=t<8||t==97;
  g.clk0[0]=levels[0];g.clk1[0]=levels[1];g.data0[0]=data[0];g.data1[0]=data[1];
  g.address0[0]=addr[0];g.address1[0]=addr[1];g.enable0[0]=en[0];g.enable1[0]=en[1];g.reset0[0]=g.reset1[0]=reset;
  for(unsigned i=0;i<2;++i){if(levels[i]&&!previous[i]){if(reset)q[i]=0;else if(en[i])q[i]=(q[i]+data[i])%256;if(en[i])ram[i][addr[i]]=data[i];}previous[i]=levels[i];}
  g.step();
  if(g.q0[0]!=q[0]||g.q1[0]!=q[1]||g.r0[0]!=ram[0][addr[0]]||g.r1[0]!=ram[1][addr[1]]){
   std::printf("mismatch t=%u q=%u/%u expected=%u/%u ram=%u/%u expected=%u/%u\n",t,g.q0[0],g.q1[0],q[0],q[1],g.r0[0],g.r1[0],ram[0][addr[0]],ram[1][addr[1]]);return 1;}
  // Repeated settle/commit at the same levels cannot invent a clock edge.
  g.step();if(g.q0[0]!=q[0]||g.q1[0]!=q[1])return 2;
 }
 puts("PASS linked graphs: clocks, state, RAM isolation and repeated settling");
}
''')
    flags = [a.cxx, '-std=c++23', '-O1', '-pthread', '-I'+str(include)]
    subprocess.run([*flags, str(w/'emit.cpp'), '-o', str(w/'emit')], check=True)
    for threads in (1, 6):
        subprocess.run([w/'emit', w/'model.h', str(threads)], check=True)
        subprocess.run([*flags, str(w/'run.cpp'), '-o', str(w/'run')], check=True)
        subprocess.run([w/'run'], check=True, timeout=30)
