"""Check library-only inclusion, dependency isolation, and the external test Makefile."""
import argparse
from pathlib import Path
import re
import subprocess
import tempfile
from TestToolchain import TestToolchain

parser = argparse.ArgumentParser()
parser.add_argument('--cxx', required=True)
parser.add_argument('--library-only', action='store_true')
args = parser.parse_args()
root = Path(__file__).resolve().parents[1]
collections = ('lib',) if args.library_only else ('lib', 'examples')
headers = sorted(p for collection in collections for p in (root / collection).glob('*/*.h')
                 if re.search(r'^int main_from_header\(', p.read_text(), re.M))
expected = 11 if args.library_only else 21
if len(headers) < expected:
    raise RuntimeError(f'Expected at least {expected} reusable headers, found {len(headers)}')
with tempfile.TemporaryDirectory(prefix='cpphdl-header-tests-') as directory:
    work = Path(directory)
    toolchain = TestToolchain(args.cxx, None, work)
    groups = [[p] for p in headers]
    groups.append(headers)
    groups.append([root / 'lib/math' / p for p in ('Fp32Mac.h', 'FpAdd.h', 'Fp32Bits.h')])
    groups += [[root / p for p in group] for group in (
        ('lib/basic/Fifo.h', 'lib/basic/Memory.h', 'lib/basic/Buffer.h'),
        ('lib/basic/Memory.h', 'lib/basic/Fifo.h'),
        ('lib/memory/MatrixTransfer.h', 'lib/memory/SynchronousRam.h', 'lib/math/FpConverter.h'))]
    if not args.library_only:
        groups.append([root / 'examples/avalon/AvalonEndpoint.h', root / 'lib/basic/Buffer.h'])
    for group in groups:
        # A leaked test entry point or clock definition makes this fail to compile.
        source = ''.join(f'#include "{p}"\n#include "{p}"\n' for p in group)
        source += 'int main_from_header = 0;\nlong _system_clock = 0;\nint main() { return 0; }\n'
        subprocess.run([args.cxx, '-std=c++23', '-I' + str(root / 'include'),
                        '-fsyntax-only', '-x', 'c++', '-'], input=source, text=True,
                       env=toolchain.env, check=True, timeout=60)
    # Memory controllers must not expose the global legacy FP wrapper names:
    # clients such as cpuml define their own FP16E5/FP32E8 value types.
    memory_include = '#include "' + str(root / 'lib/memory/MatrixTransfer.h') + '"\n'
    client_types = 'struct FP16E5 {}; struct FP32E8 {};\n'
    for source in (client_types + memory_include, memory_include + client_types):
        subprocess.run([args.cxx, '-std=c++23', '-I' + str(root / 'include'),
                        '-fsyntax-only', '-x', 'c++', '-'], input=source, text=True,
                       env=toolchain.env, check=True, timeout=60)
    selected = ['lib/math/Fp32Mac.h', 'lib/basic/Fifo.h', 'lib/memory/MatrixTransfer.h']
    for header in selected:
        source = ('#define MAINFILE 17\n#include "' + str(root / header) +
                  '"\n#if MAINFILE != 17\n#error MAINFILE was not restored\n#endif\n' +
                  '#include "' + str(root / 'tests/ExampleMain.cpp') + '"\n')
        subprocess.run([args.cxx, '-std=c++23', '-I' + str(root / 'include'),
                        '-fsyntax-only', '-x', 'c++', '-'], input=source, text=True,
                       env=toolchain.env, check=True, timeout=60)
    collection = 'lib' if args.library_only else 'examples'
    example = 'memory/MaskedSynchronousRam.h' if args.library_only else 'math/Maximum.h'
    subprocess.run(['make', '-f', str(root / collection / 'Makefile'),
                    'EXAMPLE=' + example, 'CXX=' + args.cxx,
                    'WORK=' + str(work / 'make'), 'native'],
                   env=toolchain.env, check=True, timeout=120)
print(f'PASS: {len(headers)} library headers, repeated/nested includes, and MAINFILE Makefile execution')
