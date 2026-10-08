"""Differential checks for preserved ROMs, including wide rows and index wrap."""
import argparse
import os
from pathlib import Path
import subprocess
import tempfile

parser = argparse.ArgumentParser()
for name in ('cpphdl', 'cxx', 'work'):
    parser.add_argument('--' + name, required=True)
args = parser.parse_args()
root = Path(__file__).resolve().parents[2]
Path(args.work).mkdir(parents=True, exist_ok=True)
work = Path(tempfile.mkdtemp(prefix='rom-', dir=args.work))
env = dict(os.environ)
env['LD_LIBRARY_PATH'] = str(Path(args.cxx).resolve().parent.parent / 'lib') + ':' + env.get('LD_LIBRARY_PATH', '')

def run(command):
    result = subprocess.run(list(map(str, command)), env=env, text=True, stdout=subprocess.PIPE,
                            stderr=subprocess.STDOUT, timeout=240)
    if result.returncode:
        raise RuntimeError(result.stdout)
    return result.stdout

source = work / 'Rom.cc'
source.write_text(r'''
#include <cpphdl.h>
namespace firtool_cpphdl {
template<size_t W, size_t N, class Index>
cpphdl::logic<W> array_get(const cpphdl::logic<W*N>& data, const Index& index) {
 cpphdl::logic<W> result=0; size_t first=static_cast<uint64_t>(index)*W;
 for(size_t bit=0;bit<W;++bit) if(first+bit<W*N) result.set(bit,data.get(first+bit));
 return result;
}
}
template<size_t W> constexpr cpphdl::logic<W> contents(uint64_t seed) {
 cpphdl::logic<W> result=0;
 for(size_t bit=0;bit<W;++bit)
  result.set(bit,((uint64_t(bit/64)*0x9e3779b97f4a7c15ull)^seed)>>(bit%64)&1);
 return result;
}
class Rom : public cpphdl::Module { public:
 _PORT(cpphdl::logic<64>) index_in, data_in;
 _PORT(cpphdl::logic<5>) small_in;
 _PORT(cpphdl::logic<64>) word_out, second_out, duplicate_out, dynamic_out;
 _PORT(cpphdl::logic<9>) narrow_out, wrapping_narrow_out;
 _PORT(cpphdl::logic<65>) wide_out;
 _PORT(cpphdl::logic<129>) wider_out;
 inline static constexpr auto rom=contents<32768>(0x123456789abcdef0ull);
 inline static constexpr auto second=contents<32768>(0xfedcba9876543210ull);
 inline static constexpr auto narrow=contents<27>(0x14351b5);
 inline static constexpr auto wide=contents<195>(0xabcdef0123456789ull);
 inline static constexpr auto wider=contents<645>(0xface01234567beefull);
 void _assign() {
  word_out=_ASSIGN((firtool_cpphdl::array_get<64,512>(rom,index_in())));
  duplicate_out=_ASSIGN((firtool_cpphdl::array_get<64,512>(rom,index_in())));
  second_out=_ASSIGN((firtool_cpphdl::array_get<64,512>(second,index_in())));
  narrow_out=_ASSIGN((firtool_cpphdl::array_get<9,3>(narrow,small_in())));
  wrapping_narrow_out=_ASSIGN((firtool_cpphdl::array_get<9,3>(narrow,index_in())));
  wide_out=_ASSIGN((firtool_cpphdl::array_get<65,3>(wide,small_in())));
  wider_out=_ASSIGN((firtool_cpphdl::array_get<129,5>(wider,small_in())));
  dynamic_out=_ASSIGN((firtool_cpphdl::array_get<64,2>(
   cpphdl::logic<128>(cpphdl::cat<64,64>(data_in(),cpphdl::logic<64>(0x87654321))),index_in())));
 }
};
extern Rom cpphdl_top;
#ifdef CHECK_ROM
#include "model.h"
#include <cstdio>
long _system_clock=0;
template<size_t W, size_t N> bool equal(const cpphdl::logic<W>& original,const std::array<uint32_t,N>& actual) {
 for(size_t bit=0;bit<W;++bit) if(original.get(bit)!=((actual[bit/32]>>(bit%32))&1)) return false;
 return true;
}
int main() {
 Rom original; cpphdl_native::Model model;
 cpphdl::logic<64> index=0,data=0; cpphdl::logic<5> small=0;
 original.index_in=_ASSIGN(index);original.data_in=_ASSIGN(data);original.small_in=_ASSIGN(small);original._assign();
 for(unsigned sample=0;sample<4096;++sample) {
  uint64_t address=sample<1024?sample:(uint64_t(1)<<(sample%64))+(sample%17);
  // Multiplication by nine wraps to a small, potentially unaligned bit offset.
  if(sample>=4000) address=0x8e38e38e38e38e39ull*(sample-4000);
  index=address;small=sample%32;data=sample*0xabc987651ull;
  model.index[0]=address;model.index[1]=address>>32;model.small[0]=sample%32;
  model.data[0]=uint64_t(data);model.data[1]=uint64_t(data)>>32;
  ++_system_clock;model.eval();
  if(!equal(original.word_out(),model.word) || !equal(original.second_out(),model.second) ||
     !equal(original.duplicate_out(),model.duplicate) || !equal(original.narrow_out(),model.narrow) ||
     !equal(original.wrapping_narrow_out(),model.wrapping_narrow) ||
     !equal(original.wide_out(),model.wide) || !equal(original.wider_out(),model.wider) ||
     !equal(original.dynamic_out(),model.dynamic)) {std::printf("mismatch sample %u\n",sample);return 1;}
 }
 std::puts("ROM lookups: 4096 differential samples, wide rows, OOB, wrapping indices and dynamic fallback PASS");
}
#endif
''')
graph = work / 'rom.graph'
print(run([args.cpphdl, '--lower-cpp-graph', source, graph, 'cpphdl_top', '--', '-std=c++23', '-I'+str(root/'include')]))
# Load/save/load exercises the serialized contents rather than just direct emission.
(work/'emit.cpp').write_text(r'''
#include <cpphdl_graph_native.h>
#include <cassert>
using namespace cpphdl::graph;
int main(int argc,char**argv) {
 std::ifstream f(argv[1]);std::ostringstream text;text<<f.rdbuf(); Graph g;g.load(text.str().c_str());
 assert(g.memories.size()==5); assert(g.nodes.size()<1000);
 std::ostringstream saved;g.save(saved);Graph copy;copy.load(saved.str().c_str());
 assert(copy.memories.size()==5);
 for(size_t i=0;i<g.memories.size();++i) assert(g.memories[i].contents==copy.memories[i].contents);
 copy.emit(argv[2],std::stoul(argv[3]));
 // Backward compatibility for old records without ROM metadata.
 auto old=saved.str(); old.resize(old.find("roms_v1")); Graph legacy;legacy.load(old.c_str());
 assert(legacy.memories.front().contents.empty());
 // Immutable storage must reject writes.
 g.memoryWrites.push_back({0,constant(0,64),Value(g.memories[0].width,0),{1}});
 bool rejected=false;try{g.validateClocks();}catch(const std::runtime_error&){rejected=true;}assert(rejected);
}
''')
print(run([args.cxx,'-std=c++23','-O1','-I'+str(root/'include'),work/'emit.cpp','-o',work/'emit']))
for chunk in (0,2):
    print(run([work/'emit',graph,work/'model.h',str(chunk)]))
    model=(work/'model.h').read_text()
    assert model.count('inline static constexpr std::array<std::array<uint64_t,') == 5
    print(run([args.cxx,'-std=c++23','-O2','-DCHECK_ROM','-I'+str(root/'include'),source,'-o',work/'check']))
    print(run([work/'check']))

# A name alone is not a memory contract. An edited helper keeps its actual
# C++ behavior and must not take the ROM intrinsic path.
changed = work/'changed.cc'
changed.write_text(source.read_text().replace('32768','256').replace('512','4').replace(
    'result.set(bit,data.get(first+bit))', 'result.set(bit,data.get(first+bit)^1)'))
output = work/'changed'
print(run([args.cpphdl,'--native-graph','--top','cpphdl_top','--output',output,
           '--cxx',args.cxx,changed]))
assert 'inline static constexpr std::array<std::array<uint64_t,' not in (output/'model.h').read_text()
# The original directory also contains model.h; use a source copy beside the
# changed model so quoted-include lookup tests the intended generated model.
(output/'check.cc').write_text(changed.read_text())
print(run([args.cxx,'-std=c++23','-O2','-DCHECK_ROM','-I'+str(root/'include'),
           output/'check.cc','-o',output/'check']))
print(run([output/'check']))
print('edited array_get body retains ordinary C++ semantics PASS')
