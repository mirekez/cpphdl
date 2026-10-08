"""Check narrowing barrel shifters, ROM reads, and oversized shift amounts."""
import argparse
from pathlib import Path
import subprocess
import tempfile

parser = argparse.ArgumentParser()
parser.add_argument('--cxx', required=True)
args = parser.parse_args()
root = Path(__file__).resolve().parents[2]
work = Path(tempfile.mkdtemp(prefix='cpphdl-wide-shift-'))

def run(command):
    result = subprocess.run(list(map(str, command)), text=True, capture_output=True, timeout=180)
    if result.returncode:
        raise RuntimeError(result.stdout + result.stderr)
    return result.stdout

(work / 'emit.cpp').write_text(r'''
#include <cpphdl_graph_native.h>
using namespace cpphdl::graph;
int main(int argc, char** argv) {
 Graph graph;
 auto data=graph.wire(129,"data","input"), shift=graph.wire(64,"shift","input");
 graph.ports.push_back({"data",data,true}); graph.ports.push_back({"shift",shift,true});
 for(auto op : {"shr","sar"}) for(unsigned width : {1,64,129,193})
  graph.ports.push_back({std::string(op)+std::to_string(width),graph.binary(op,data,shift,width),false});
 Value rom(32768);
 for(size_t bit=0;bit<rom.size();++bit)
  rom[bit]=((uint64_t(bit/64)*0x9e3779b97f4a7c15ull)^0x123456789abcdef0ull)>>(bit%64)&1;
 auto before=graph.nodes.size();
 auto selected=graph.binary("shr",rom,shift,1);
 if(graph.nodes.size()-before>2000) return 1;
 graph.ports.push_back({"rom",selected,false});
 graph.compact(); graph.emit(argv[1],32);
}
''')
print(run([args.cxx, '-std=c++23', '-O1', '-I' + str(root / 'include'), work / 'emit.cpp', '-o', work / 'emit']))
print(run([work / 'emit', work / 'model.h']))
(work / 'check.cpp').write_text(r'''
#include "model.h"
#include <cstdio>
template<size_t N> bool check(const std::array<uint32_t,N>& out, const cpphdl_native::Model& m,
                            unsigned width, uint64_t shift, bool sign) {
 for(unsigned bit=0;bit<width;++bit) {
  bool expected=sign && (m.data[4]&1);
  if(shift<129 && bit<129-shift) {
   unsigned source=bit+shift; expected=(m.data[source/32]>>(source%32))&1;
  }
  if(bool((out[bit/32]>>(bit%32))&1)!=expected) return false;
 }
 return true;
}
int main() {
 cpphdl_native::Model model;
 for(unsigned sample=0;sample<33024;++sample) {
  uint64_t shift=sample<32768 ? sample : (uint64_t(1)<<(sample%64)) + sample%3;
  model.shift[0]=shift; model.shift[1]=shift>>32;
  for(unsigned i=0;i<5;++i) model.data[i]=(sample*0x1234567u)^(0xfedcba98u>>(i%8));
  model.data[4]=sample&1; model.eval();
  bool expected=shift<32768 && (((shift/64*0x9e3779b97f4a7c15ull)^0x123456789abcdef0ull)>>(shift%64)&1);
  if(model.rom[0]!=expected) return 1;
#define CHECK(OP,W,S) if(!check(model.OP##W,model,W,shift,S)) return 2;
  CHECK(shr,1,false) CHECK(shr,64,false) CHECK(shr,129,false) CHECK(shr,193,false)
  CHECK(sar,1,true) CHECK(sar,64,true) CHECK(sar,129,true) CHECK(sar,193,true)
#undef CHECK
 }
 std::puts("wide shifts and 32768-bit ROM: 33024 samples PASS");
}
''')
print(run([args.cxx, '-std=c++23', '-O2', work / 'check.cpp', '-o', work / 'check']))
print(run([work / 'check']))
