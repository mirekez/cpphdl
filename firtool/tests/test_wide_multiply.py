#!/usr/bin/env python3
"""Check FIRRTL multiplication above 64 bits in plain C++ and native graph."""
import argparse
from pathlib import Path
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[2]
FIR = '''circuit WideMultiply:
  module WideMultiply:
    input lhs: UInt<108>
    input rhs: UInt<108>
    output product: UInt<108>
    product <= bits(mul(lhs, rhs), 107, 0)
'''
EMIT = '''#include <cpphdl_graph_native.h>
#include <fstream>
#include <sstream>
int main(int argc, char** argv) {
  cpphdl::graph::Graph graph;
  std::ifstream input(argv[1]); std::ostringstream text; text << input.rdbuf();
  graph.load(text.str().c_str()); graph.emit(argv[2], 256, 1);
}
'''
CHECK = '''#include "WideMultiply.cpp"
#include <cpphdl_graph_ports.h>
#include <cassert>
#include <cstdio>
#ifdef CHECK_NATIVE
#include "model.h"
#endif
long _system_clock = 0;
using U128 = __uint128_t;
template<size_t Width> cpphdl::logic<Width> bits(U128 value) {
  cpphdl::logic<Width> result = 0;
  for (unsigned i=0; i<Width; ++i) result.set(i, (value >> i) & 1);
  return result;
}
template<size_t Width> U128 number(const cpphdl::logic<Width>& value) {
  U128 result = 0;
  for (unsigned i=0; i<Width; ++i) result |= U128(value.get(i)) << i;
  return result;
}
int main() {
  WideMultiply dut;
  cpphdl::logic<108> lhs = 0, rhs = 0;
  dut.lhs = _ASSIGN_REG(lhs); dut.rhs = _ASSIGN_REG(rhs); dut._assign();
#ifdef CHECK_NATIVE
  cpphdl_native::Model graph;
#endif
  uint64_t state = 0x243f6a8885a308d3ULL;
  auto random = [&]() { state ^= state<<13; state ^= state>>7; state ^= state<<17; return state; };
  const U128 mask = (U128(1)<<108)-1;
  for (unsigned i=0; i<1000; ++i) {
    const U128 a = i == 0 ? U128(10)<<32 : (U128(random())<<64 | random()) & mask;
    const U128 b = i == 0 ? U128(0xcccccccd)<<32 : (U128(random())<<64 | random()) & mask;
    lhs = bits<108>(a); rhs = bits<108>(b); ++_system_clock;
    assert(number(dut.product()) == (a*b & mask));
    assert(number(firtool_cpphdl::multiply<73>(bits<73>(a), bits<73>(b))) == (a*b & ((U128(1)<<73)-1)));
#ifdef CHECK_NATIVE
    cpphdl::graph_runtime::pack(graph.lhs, lhs); cpphdl::graph_runtime::pack(graph.rhs, rhs);
    graph.eval(false);
    cpphdl::logic<108> result;
    cpphdl::graph_runtime::unpack(result, graph.product);
    assert(number(result) == (a*b & mask));
#endif
  }
  puts("Wide multiplication: 73/108-bit carries and 1000 randomized products PASSED");
}
'''


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--firtool', required=True, type=Path)
    parser.add_argument('--cpphdl', type=Path, default=ROOT / 'build/cpphdl')
    parser.add_argument('--cxx', default='g++')
    args = parser.parse_args()
    with tempfile.TemporaryDirectory(prefix='cpphdl-wide-multiply-') as tmp:
        work = Path(tmp)

        def run(*command):
            subprocess.run(list(map(str, command)), cwd=work, check=True)

        (work/'test.fir').write_text(FIR)
        run(args.firtool.resolve(), '--split-cpphdl', '-o='+str(work/'generated'), work/'test.fir')
        (work/'check.cpp').write_text(CHECK)
        flags = ['-std=c++23', '-O2', '-I'+str(ROOT/'include'), '-I'+str(work/'generated')]
        run(args.cxx, *flags, work/'check.cpp', '-o', work/'plain')
        run(work/'plain')
        (work/'partition.cpp').write_text('''#include "WideMultiply.cpp"
class Partition : public cpphdl::Module { public:
  WideMultiply dut;
  _PORT(cpphdl::logic<108>) lhs_in, rhs_in, product_out;
  void _assign() {
    dut.lhs = _ASSIGN(lhs_in()); dut.rhs = _ASSIGN(rhs_in()); dut._assign();
    product_out = _ASSIGN(dut.product());
  }
  void _work(bool reset) { dut._work(reset); }
  void _strobe() { dut._strobe(); }
};
extern Partition cpphdl_top;
''')
        run(args.cpphdl.resolve(), '--lower-cpp-graph', work/'partition.cpp', work/'module.graph',
            'cpphdl_top', '--', *flags)
        (work/'emit.cpp').write_text(EMIT)
        run(args.cxx, *flags, work/'emit.cpp', '-o', work/'emit')
        run(work/'emit', work/'module.graph', work/'model.h')
        run(args.cxx, *flags, '-DCHECK_NATIVE', work/'check.cpp', '-o', work/'native')
        run(work/'native')


if __name__ == '__main__':
    main()
