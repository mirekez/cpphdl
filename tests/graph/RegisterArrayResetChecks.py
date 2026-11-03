"""Register-array reset preserves adjacent elements and aggregate extents."""
import argparse
from pathlib import Path
import subprocess
import tempfile
p=argparse.ArgumentParser()
for name in ('cpphdl','cxx','work'):p.add_argument('--'+name,required=True)
a=p.parse_args();root=Path(__file__).resolve().parents[2]
Path(a.work).mkdir(parents=True,exist_ok=True);work=Path(tempfile.mkdtemp(prefix='reset-',dir=a.work))
def run(cmd):
    result=subprocess.run(list(map(str,cmd)),capture_output=True,text=True,timeout=240)
    if result.returncode:raise RuntimeError(str(cmd)+'\n'+result.stdout+result.stderr)
source=work/'bank.cpp'
source.write_text('''#include <cpphdl.h>
class Bank:public cpphdl::Module{public:
 _PORT(uint32_t) data_in;_PORT(uint32_t) index_in;_PORT(bool) clear_in;
 _PORT(cpphdl::logic<128>) values_out;
 cpphdl::reg<cpphdl::u32> words[4];cpphdl::logic<128> values;
 cpphdl::logic<128>& values_func(){for(unsigned i=0;i<4;i++)values.bits(i*32+31,i*32)=words[i];return values;}
 void _assign(){values_out=_ASSIGN_COMB(values_func());}
#ifdef ASYNC
 void _work_fast_clk(bool){for(unsigned i=0;i<4;i++)words[i]._next=data_in()+i;}
 void _reset_pos_fast_clk(){for(unsigned i=0;i<4;i++)words[i].set(0x1100u+i);}
 void _strobe_fast_clk(){for(unsigned i=0;i<4;i++)words[i].strobe();}
#else
 void _work(bool reset){
  for(unsigned i=0;i<4;i++)words[i]._next=data_in()+i;
  if(clear_in())words[index_in()%4].clr();
  if(reset)for(unsigned i=0;i<4;i++)words[i].clr();
 }
 void _strobe(){for(unsigned i=0;i<4;i++)words[i].strobe();}
#endif
};Bank bank;
''')
run([a.cpphdl,'--native-graph','--cpphdl',a.cpphdl,'--top','bank','--output',work/'native','--cxx',a.cxx,source])
(work/'check.cpp').write_text('''#include "native/model.h"
#include <cassert>
int main(){cpphdl_native::Model m;
for(unsigned t=0;t<1024;t++){m.data[0]=t*7919;m.index[0]=t%4;m.clear[0]=t%3>0;m.work_reset[0]=t%97==0;m.step();
for(unsigned i=0;i<4;i++)assert(m.values[i]==((m.work_reset[0]||(m.clear[0]&&i==m.index[0]))?0:m.data[0]+i));}}
''')
flags=[a.cxx,'-std=c++23','-O1','-I'+str(root/'include')]
run([*flags,work/'check.cpp','-o',work/'check']);run([work/'check'])
run([a.cpphdl,'--lower-cpp-graph',source,work/'async.graph','bank','--final-native','--clock','fast_clk','100000000','--','-I'+str(root/'include'),'-DASYNC'])
(work/'audit.cpp').write_text('''#define CPPHDL_GRAPH_CORE_ONLY
#include <cpphdl_graph.h>
#include <fstream>
#include <sstream>
#include <cassert>
int main(int argc,char**argv){std::ifstream f(argv[1]);std::ostringstream s;s<<f.rdbuf();cpphdl::graph::Graph g;g.load(s.str().c_str());
unsigned total=0;for(auto& state:g.states){if(state.reset.empty())continue;
for(unsigned i=0;i<state.resetValue.size();i+=32){auto value=cpphdl::graph::number(cpphdl::graph::slice(state.resetValue,i,32));assert(value && *value==0x1100+total);++total;}}
assert(total==4);}
''')
run([*flags,work/'audit.cpp','-o',work/'audit']);run([work/'audit',work/'async.graph'])
print('PASS native register arrays: 1024 dynamic clear/reset clocks; full-width asynchronous set')
