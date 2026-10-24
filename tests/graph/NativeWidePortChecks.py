"""A packed output must expose independent word cones to native workers."""
import argparse
from pathlib import Path
import subprocess
import sys

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))
from TestToolchain import TestToolchain
import tempfile

p=argparse.ArgumentParser();p.add_argument('--cxx',required=True);a=p.parse_args()
root=Path(__file__).resolve().parents[2]
with tempfile.TemporaryDirectory(prefix='cpphdl-wide-port-') as temporary:
    work=Path(temporary)
    toolchain = TestToolchain(a.cxx, None, work)
    def run(args):
        result=subprocess.run(toolchain.command(args),env=toolchain.env,capture_output=True,text=True,timeout=240)
        if result.returncode:raise RuntimeError(result.stdout+result.stderr)
        return result.stdout
    (work/'emit.cpp').write_text(r'''
#include <cpphdl_graph_native.h>
using namespace cpphdl::graph;
int main(int argc,char** argv){
 Graph g;auto data=g.wire(64,"data","input");g.ports.push_back({"data",data,true});Value packed;
 for(unsigned branch=0;branch<16;++branch){
  auto value=g.binary("xor",data,constant(branch*19+1,64),64);
  for(unsigned i=0;i<48;++i){value=g.binary("add",value,data,64);
   value=g.binary("xor",value,constant(i*7919+branch,64),64);}
  packed.insert(packed.end(),value.begin(),value.end());
 }
 packed.push_back(data[63]);g.ports.push_back({"packed",packed,false});
 g.emit(std::string(argv[1])+"serial.h",0,1);
 g.emit(std::string(argv[1])+"parallel.h",17,6,false,false,true);
}
''')
    run([a.cxx,'-std=c++23','-O1','-I'+str(root/'include'),work/'emit.cpp','-o',work/'emit'])
    run([work/'emit',str(work)+'/'])
    (work/'check.cpp').write_text(r'''
#define cpphdl_native serial
#include "serial.h"
#undef cpphdl_native
#define cpphdl_native parallel
#include "parallel.h"
#undef cpphdl_native
#include <cassert>
#include <cstdio>
int main(){
 static_assert(parallel::Model::__cpphdl_thread_count==6);
 serial::Model reference;parallel::Model threaded;uint64_t random=42;
 for(unsigned i=0;i<2048;++i){
  if(i%7==0){random^=random<<13;random^=random>>7;random^=random<<17;}
  reference.data=threaded.data={uint32_t(random),uint32_t(random>>32)};
  using P=cpphdl::graph_runtime::DispatchPolicy;
  threaded.__cpphdl_dispatch_policy=i%3==0?P::Serial:i%3==1?P::Parallel:P::Automatic;
  for(bool commit:{false,true,false}){reference.eval(commit);threaded.eval(commit);assert(reference.packed==threaded.packed);}
  for(unsigned branch=0;branch<16;++branch){
   uint64_t want=random^(branch*19+1);
   for(unsigned j=0;j<48;++j){want+=random;want^=j*7919+branch;}
   assert(threaded.packed[branch*2]==uint32_t(want));assert(threaded.packed[branch*2+1]==uint32_t(want>>32));
  }
  assert(threaded.packed[32]==(random>>63));
  if(i%31==0){auto copy=threaded;copy.eval();assert(copy.packed==reference.packed);}
 }
 std::puts("PASS wide native output: six lanes for one 1025-bit port, independent oracle, partial word, copies and serial/parallel/auto dispatch");
}
''')
    run([a.cxx,'-std=c++23','-O1','-pthread','-fsanitize=undefined','-fno-sanitize-recover=all',
         '-I'+str(root/'include'),work/'check.cpp','-o',work/'check'])
    print(run([work/'check']),end='')
