"""Check native/host port transfers against bit semantics, including padding."""
import argparse
from pathlib import Path
import subprocess
import tempfile

p = argparse.ArgumentParser()
p.add_argument('--cxx', required=True)
a = p.parse_args()
root = Path(__file__).resolve().parents[2]
with tempfile.TemporaryDirectory(prefix='cpphdl-native-ports-') as tmp:
    work = Path(tmp)
    (work/'check.cpp').write_text(r'''
#include <cpphdl.h>
// Load the standard library with the real platform macros, then force only
// the boundary helper through its endian-independent implementation.
#ifdef CHECK_PORTABLE
#pragma push_macro("__BYTE_ORDER__")
#undef __BYTE_ORDER__
#endif
#include <cpphdl_graph_ports.h>
#ifdef CHECK_PORTABLE
#pragma pop_macro("__BYTE_ORDER__")
#endif
#include <cassert>
#include <cstdio>
template<size_t W> void check() {
 uint64_t rng=42;
 auto random=[&] {rng^=rng<<13;rng^=rng>>7;rng^=rng<<17;return rng;};
 for(unsigned trial=0;trial<256;++trial) {
  cpphdl::logic<W> value;
  for(auto& byte:value.bytes) byte=random(); // includes poisoned padding
  std::array<uint32_t,(W+31)/32+2> words,reference{};
  words.fill(~0u);
  for(size_t bit=0;bit<W;++bit) if(value.get(bit)) reference[bit/32]|=uint32_t(1)<<(bit%32);
  cpphdl::graph_runtime::pack(words,value);
  assert(words==reference);
  for(auto& word:words) word=random(); // arbitrary word and padding contents
  cpphdl::logic<W> decoded,expected=0;
  for(auto& byte:decoded.bytes) byte=0xff;
  for(size_t bit=0;bit<W;++bit) expected.set(bit,(words[bit/32]>>(bit%32))&1);
  cpphdl::graph_runtime::unpack(decoded,words);
  for(size_t byte=0;byte<decoded.SIZE;++byte) assert(decoded.bytes[byte]==expected.bytes[byte]);
 }
}
int main() {
 check<1>();check<3>();check<7>();check<8>();check<9>();check<31>();check<32>();check<33>();
 check<63>();check<64>();check<65>();check<127>();check<128>();check<129>();
 check<512>();check<513>();check<1537>();
 std::puts("native ports: bit equivalence, wide values and poisoned padding PASS");
}
''')
    for portable in (False, True):
        subprocess.run([a.cxx,'-std=c++23','-O2','-fsanitize=undefined','-fno-sanitize-recover=all',
                        *(['-DCHECK_PORTABLE'] if portable else []),'-I'+str(root/'include'),
                        str(work/'check.cpp'),'-o',str(work/'check')],check=True)
        subprocess.run([str(work/'check')],check=True)
