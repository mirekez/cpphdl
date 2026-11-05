#!/usr/bin/env python3
"""The ordinary FP RTL headers must be reusable without the HLS tree."""
import argparse
from pathlib import Path
import shutil
import subprocess
import sys
import tempfile

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))
from TestToolchain import TestToolchain


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--cxx', required=True)
    args = parser.parse_args()
    root = Path(__file__).resolve().parents[2]
    library = tuple('lib/math/' + name + '.h' for name in (
        'FpConvertBits', 'FpConverter', 'FpAdd', 'Fp32Mac', 'Fp32Bits', 'Fp32Add', 'Q48Math')) + tuple(
        'lib/basic/' + name + '.h' for name in ('Buffer', 'Fifo', 'Memory')) + tuple(
        'lib/memory/' + name + '.h' for name in ('SynchronousRam', 'MaskedSynchronousRam', 'MatrixTransfer'))
    examples = tuple('examples/math/' + name + '.h' for name in (
        'FpSqrt', 'FpTanh', 'Fp32Exp', 'Maximum'))
    headers = library + examples
    with tempfile.TemporaryDirectory(prefix='cpphdl-rtl-math-') as directory:
        work = Path(directory)
        toolchain = TestToolchain(args.cxx, None, work)

        def run(command):
            subprocess.run(list(map(str, command)), cwd=work, env=toolchain.env,
                           check=True, timeout=90)

        # First compile lib/ with neither examples/ nor hls/ present. Then add
        # examples and verify their cross-directory dependencies as well.
        for group in (library, examples):
            for header in group:
                (work / header).parent.mkdir(parents=True, exist_ok=True)
                shutil.copyfile(root / header, work / header)
            for header in group:
                source = work / 'header.cc'
                source.write_text(f'#include "{header}"\n')
                run([args.cxx, '-std=c++23', '-I' + str(root / 'include'),
                     '-fsyntax-only', source])

        # Raw conversions must also work without cpphdl.h or RTL FP wrappers.
        source = work / 'raw-conversion.cc'
        source.write_text(r'''
#include "lib/math/FpConvertBits.h"
int main() {
    using namespace cpphdl_examples::math;
    if (FpFormatConversion::convert(0x3c00, 16, 5, 32, 8, true, true) != 0x3f800000u) return 1;
    if (IntFormatConversion<32, 8>::from_int_raw(-2) != 0xc0000000u) return 2;
    return 0;
}
''')
        run([args.cxx, '-std=c++17', source, '-o', work / 'raw-conversion'])
        run([work / 'raw-conversion'])

        source = work / 'reuse.cc'
        source.write_text(''.join(f'#include "{header}"\n' for header in headers) + r'''
long _system_clock = 0;
int main() {
    FP<16, 5> half = FP<16, 5>::make(false, 15, 0);
    FP<32, 8> single;
    half.convert(single);
    FpConverter<FP<16, 5>, FP<32, 8>, 1, false> converter;
    FpAdd<32, 8> add;
    FpSqrt<16, 5> square_root;
    FpTanh<32, 8> tanh;
    Fp32Mac mac;
    cpphdl_examples::SynchronousRam<1, 8> ram;
    cpphdl_examples::MaskedSynchronousRam<3, 17> masked;
    cpphdl_examples::MatrixTransfer transfer;
    converter._assign();
    add._assign();
    square_root._assign();
    tanh._assign();
    mac._assign();
    ram._assign();
    masked._assign();
    transfer._assign();
    if (cpphdl_examples::math::fp32_add(0x3f800000u, 0x3f800000u) != 0x40000000u) return 2;
    return single.raw == 0x3f800000u ? 0 : 1;
}
''')
        run([args.cxx, '-std=c++23', '-I' + str(root / 'include'),
             source, '-o', work / 'reuse'])
        run([work / 'reuse'])
    print(f'PASS: {len(headers)} standalone RTL headers compile and run without HLS')


if __name__ == '__main__':
    main()
