"""Whole-array NBA banks must share storage with indexed reads and writes."""
import argparse
from pathlib import Path
import subprocess
import tempfile
p=argparse.ArgumentParser()
for name in ('hdlcpp','cxx','work'):p.add_argument('--'+name,required=True)
a=p.parse_args();root=Path(__file__).resolve().parents[2]
Path(a.work).mkdir(parents=True,exist_ok=True);work=Path(tempfile.mkdtemp(prefix='bank-',dir=a.work))
def run(cmd):
    result=subprocess.run(list(map(str,cmd)),capture_output=True,text=True,timeout=240)
    if result.returncode:raise RuntimeError(str(cmd)+'\n'+result.stdout+result.stderr)
(work/'bank.sv').write_text('''module Bank(input clk,reset,enable,input [1:0] index,input [31:0] data,
 output [31:0] q,r);
 reg [31:0] bank[0:3],temporary[0:3],ram[0:3];
 task update;
   temporary=bank;
   if(reset)for(int i=0;i<4;i++)temporary[i]=0;
   else if(enable)begin temporary[0]=data;for(int i=1;i<4;i++)temporary[i]=bank[i-1];end
 endtask
 always @(posedge clk) begin
   update();bank<=temporary;
   if(reset)for(int i=0;i<4;i++)ram[i]<=0;
   else if(enable)ram[index]<=data;
 end
 assign q=bank[index];assign r=ram[index];
 endmodule''')
run([a.hdlcpp,'--native-graph','--top','Bank','--output',work/'graph.cpp',work/'bank.sv'])
flags=[a.cxx,'-std=c++23','-O1','-I'+str(root/'include')]
run([*flags,work/'graph.cpp','-o',work/'emit']);run([work/'emit',work/'model.h'])
(work/'check.cpp').write_text('''#include "model.h"
#include <array>
#include <cassert>
int main(){cpphdl_native::Model m;std::array<unsigned,4> bank{},ram{};
for(unsigned t=0;t<4096;t++){
 unsigned i=t%4,data=t*7919;bool reset=t%97==0,en=t%7>2;
 m.clk[0]=0;m.reset[0]=reset;m.enable[0]=en;m.index[0]=i;m.data[0]=data;m.step();
 assert(m.q[0]==bank[i] && m.r[0]==ram[i]);
 if(reset){bank={};ram={};}else if(en){for(unsigned j=3;j>0;j--)bank[j]=bank[j-1];bank[0]=data;ram[i]=data;}
 m.clk[0]=1;m.step();assert(m.q[0]==bank[i] && m.r[0]==ram[i]);
 m.step();assert(m.q[0]==bank[i] && m.r[0]==ram[i]);
}}
''')
run([*flags,work/'check.cpp','-o',work/'check']);run([work/'check'])
print('PASS whole-array register bank and indexed RAM: 4096 clocks, task, reset, stalls, duplicate edges')
