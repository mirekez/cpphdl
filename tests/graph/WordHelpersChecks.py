"""Compare word helper lowering with executed C++, including unsafe wrap fallback."""
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
work = Path(tempfile.mkdtemp(prefix='words-', dir=args.work))
env = dict(os.environ)
env['LD_LIBRARY_PATH'] = str(Path(args.cxx).resolve().parent.parent / 'lib') + ':' + env.get('LD_LIBRARY_PATH', '')

def run(command):
    p = subprocess.run(list(map(str, command)), env=env, text=True, capture_output=True, timeout=240)
    if p.returncode:
        raise RuntimeError(p.stdout + p.stderr)
    return p.stdout

cases = [(f's{w}', w, f'firtool_cpphdl::ashr<{w}>(cpphdl::logic<{w}>(data_in()), shift_in())')
         for w in (7, 32, 64, 65, 78, 165)]
cases += [('wrap_shift', 9, 'firtool_cpphdl::ashr<9>(cpphdl::logic<9>(data_in()), index_in())')]
cases += [(f'a{w}', w, f'firtool_cpphdl::array_get<{w},4>(cpphdl::logic<{w*4}>(data_in()), small_in())')
          for w in (9, 42, 64, 65, 129)]
cases += [(f'wrap_array{w}', w, f'firtool_cpphdl::array_get<{w},4>(cpphdl::logic<{w*4}>(data_in()), index_in())')
          for w in (9, 64)]
source = r'''
#include <cpphdl.h>
namespace firtool_cpphdl {
template<size_t Width, class Shift>
cpphdl::logic<Width> ashr(const cpphdl::logic<Width>& value, const Shift& amount) {
 cpphdl::logic<Width> result=0; size_t shift=static_cast<uint64_t>(amount); bool sign=value.get(Width-1);
 for(size_t bit=0;bit<Width;++bit) result.set(bit,bit+shift<Width?value.get(bit+shift):sign);
 return result;
}
template<size_t W,size_t N,class Index>
cpphdl::logic<W> array_get(const cpphdl::logic<W*N>& data,const Index& index) {
 cpphdl::logic<W> result=0; size_t first=static_cast<uint64_t>(index)*W;
 for(size_t bit=0;bit<W;++bit) if(first+bit<W*N) result.set(bit,data.get(first+bit));
 return result;
}
}
class Words : public cpphdl::Module {public:
 _PORT(cpphdl::logic<516>) data_in;
 _PORT(cpphdl::logic<8>) shift_in;
 _PORT(cpphdl::logic<5>) small_in;
 _PORT(cpphdl::logic<64>) index_in;
'''
source += ''.join(f' _PORT(cpphdl::logic<{w}>) {name}_out;\n' for name, w, expr in cases)
source += ' void _assign() {\n'
source += ''.join(f' {name}_out=_ASSIGN(({expr}));\n' for name, w, expr in cases)
source += r'''} };
extern Words cpphdl_top;
#ifdef CHECK
#include "model.h"
#include <cstdio>
long _system_clock=0;
template<size_t W,size_t N> bool same(const cpphdl::logic<W>& a,const std::array<uint32_t,N>& b) {
 for(size_t bit=0;bit<W;++bit) if(a.get(bit)!=((b[bit/32]>>(bit%32))&1)) return false;
 return true;
}
int main() {
 Words original; cpphdl_native::Model model;
 cpphdl::logic<516> data=0; cpphdl::logic<8> shift=0;
 cpphdl::logic<5> small=0; cpphdl::logic<64> index=0;
 original.data_in=_ASSIGN(data);original.shift_in=_ASSIGN(shift);
 original.small_in=_ASSIGN(small);original.index_in=_ASSIGN(index);original._assign();
 uint64_t rng=0x123456789abcdef0ull;
 for(unsigned sample=0;sample<4096;++sample) {
  for(unsigned bit=0;bit<516;++bit) {
   rng^=rng<<13;rng^=rng>>7;rng^=rng<<17;
   data.set(bit,rng&1);
  }
  for(unsigned word=0;word<17;++word) {
   model.data[word]=0;
   for(unsigned bit=0;bit<32 && word*32+bit<516;++bit)
    model.data[word]|=uint32_t(data.get(word*32+bit))<<bit;
  }
  uint64_t address=sample<1024?sample:(uint64_t(1)<<(sample%64))+(sample%17);
  if(sample>=3000) address=0x8e38e38e38e38e39ull*(sample-3000);
  if(sample>=4000) address=~uint64_t(0)-(sample%32);
  shift=sample%256;small=sample%32;index=address;
  model.shift[0]=sample%256;model.small[0]=sample%32;
  model.index[0]=address;model.index[1]=address>>32;
  ++_system_clock;model.eval();
'''
source += ''.join(f' if(!same(original.{name}_out(),model.{name})) {{std::printf("{name} sample %u mismatch\\n",sample);return 1;}}\n'
                  for name, w, expr in cases)
source += ' } std::puts("word helpers: 4096 differential samples PASS"); }\n#endif\n'
(work/'emit.cpp').write_text(r'''
#include <cpphdl_graph_native.h>
int main(int argc,char**argv) {
 std::ifstream f(argv[1]);std::ostringstream s;s<<f.rdbuf();cpphdl::graph::Graph g;g.load(s.str().c_str());
 g.emit(argv[2],std::stoul(argv[3]));
}
''')
print(run([args.cxx,'-std=c++23','-O1','-I'+str(root/'include'),work/'emit.cpp','-o',work/'emit']))
for edited in (False, True):
    text = source.replace('bool sign=value.get(Width-1);', 'bool sign=!value.get(Width-1);') if edited else source
    (work/'Words.cc').write_text(text)
    (work/'model.graph').unlink(missing_ok=True)
    print(run([args.cpphdl,'--lower-cpp-graph',work/'Words.cc',work/'model.graph','cpphdl_top','--','-std=c++23','-I'+str(root/'include')]))
    for chunk in (0, 32):
        print(run([work/'emit',work/'model.graph',work/'model.h',str(chunk)]))
        print(run([args.cxx,'-std=c++23','-O2','-DCHECK','-I'+str(root/'include'),work/'Words.cc','-o',work/'check']))
        print(run([work/'check']))
