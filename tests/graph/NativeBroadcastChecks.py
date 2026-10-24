"""Check packed Boolean broadcasts at word boundaries against a scalar oracle."""
import argparse
from pathlib import Path
import subprocess
import sys

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))
from TestToolchain import TestToolchain
import tempfile

p=argparse.ArgumentParser();p.add_argument('--cxx',required=True);a=p.parse_args()
root=Path(__file__).resolve().parents[2]
with tempfile.TemporaryDirectory(prefix='cpphdl-broadcast-') as temporary:
    work=Path(temporary)
    toolchain = TestToolchain(a.cxx, None, work)
    def run(args):
        result=subprocess.run(toolchain.command(args),env=toolchain.env,capture_output=True,text=True,timeout=240)
        if result.returncode:raise RuntimeError(result.stdout+result.stderr)
        return result.stdout
    (work/'emit.cpp').write_text(r'''
#include <cpphdl_graph_native.h>
using namespace cpphdl::graph;
int main(int argc,char**argv){
 Graph g;g.clockContract=ClockContract::RisingEdgeStep;
 auto data=g.wire(128,"data","input");g.ports.push_back({"data",data,true});
 auto enable=g.wire(1,"enable","input");g.ports.push_back({"enable",enable,true});
 Value gathered;
 for(unsigned word=0;word<4;++word)for(unsigned bit=0;bit<64;++bit){
  unsigned source=word==0?127:word==1?(bit<3?bit:70):word==2?(bit<31?31:32):(64+bit%2);
  gathered.push_back(data[source]);
 }
 g.ports.push_back({"gathered",gathered,false});Value stateOut;
 for(unsigned word=0;word<4;++word){auto state=g.add("state",64,constant(0x8735000000000000ull+word,64));
  auto next=g.binary("xor",slice(gathered,word*64,64),constant(0xabc0123456789defull+word,64),64);
  g.states.push_back({state,g.mux(enable,next,state),{1}});stateOut.insert(stateOut.end(),state.begin(),state.end());}
 g.ports.push_back({"state_out",stateOut,false});
 auto copy=g;copy.emit(std::string(argv[1])+"plain.h");
 g.emit(std::string(argv[1])+"cached.h",7,6,false,false,true,false,true);
}
''')
    run([a.cxx,'-std=c++23','-O1','-I'+str(root/'include'),work/'emit.cpp','-o',work/'emit'])
    run([work/'emit',str(work)+'/'])
    if '* 18446744073709551615ull' not in (work/'cached.h').read_text():
        raise RuntimeError('Full-width broadcast emission was not exercised')
    (work/'check.cpp').write_text(r'''
#define cpphdl_native plain
#include "plain.h"
#undef cpphdl_native
#define cpphdl_native cached
#include "cached.h"
#undef cpphdl_native
#include <cassert>
#include <cstdio>
#include <random>
int main(){plain::Model a;cached::Model b;std::mt19937_64 rng(17);
 std::array<uint64_t,4> state{};for(unsigned w=0;w<4;++w)state[w]=0x8735000000000000ull+w;
 for(unsigned i=0;i<4096;++i){std::array<uint64_t,2> input{rng(),rng()};if(i<128){input={0,0};input[i/64]=1ull<<(i%64);}
  bool enable=i%7!=0;a.enable[0]=b.enable[0]=enable;
  for(unsigned w=0;w<4;++w)a.data[w]=b.data[w]=uint32_t(input[w/2]>>(32*(w%2)));
  std::array<uint64_t,4> expected{};
  for(unsigned word=0;word<4;++word)for(unsigned bit=0;bit<64;++bit){
   unsigned source=word==0?127:word==1?(bit<3?bit:70):word==2?(bit<31?31:32):(64+bit%2);
   if((input[source/64]>>(source%64))&1)expected[word]|=1ull<<bit;
  }
  for(bool commit:{false,true,false}){a.eval(commit);b.eval(commit);assert(a.gathered==b.gathered&&a.state_out==b.state_out);
   for(unsigned w=0;w<4;++w){assert((uint64_t(b.gathered[2*w])|(uint64_t(b.gathered[2*w+1])<<32))==expected[w]);
    assert((uint64_t(b.state_out[2*w])|(uint64_t(b.state_out[2*w+1])<<32))==state[w]);}
   if(commit&&enable)for(unsigned w=0;w<4;++w)state[w]=expected[w]^(0xabc0123456789defull+w);
  }
 }
 std::puts("PASS native Boolean broadcasts: 12288 phases, full/partial words, bit 127, alternating bits, held registers and scalar oracle");
}
''')
    run([a.cxx,'-std=c++23','-O2','-pthread','-fsanitize=undefined','-fno-sanitize-recover=all',
         '-I'+str(root/'include'),work/'check.cpp','-o',work/'check'])
    print(run([work/'check']),end='')
