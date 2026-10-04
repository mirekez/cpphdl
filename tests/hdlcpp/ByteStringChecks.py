"""String literals in integral contexts encode bytes, not C++ pointer addresses."""
import argparse
from pathlib import Path
import subprocess
import tempfile
import sys

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))
from TestToolchain import TestToolchain


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--hdlcpp', required=True)
    parser.add_argument('--cxx', required=True)
    parser.add_argument('--work', required=True, type=Path)
    parser.add_argument('--cpphdl')
    parser.add_argument('--verilator')
    args = parser.parse_args()
    fixtures = Path(__file__).resolve().parent
    include = fixtures.parents[1] / 'include'
    args.work.mkdir(parents=True, exist_ok=True)
    with tempfile.TemporaryDirectory(prefix='byte-string-', dir=args.work) as temporary:
        work = Path(temporary)
        toolchain = TestToolchain(args.cxx, args.verilator, work)

        def run(command):
            result = subprocess.run(toolchain.command(command), cwd=work, env=toolchain.env, text=True,
                                    stdout=subprocess.PIPE, stderr=subprocess.STDOUT, timeout=180)
            if result.returncode:
                logs = ''.join(path.read_text(errors='replace')[-3000:]
                               for path in work.glob('*/cpp-to-graph.log'))
                raise RuntimeError(f'{command}\n{result.stdout[-6000:]}\n{logs}')
            return result.stdout

        for name in ('ByteStringLiteral', 'ByteStringContexts'):
            source = fixtures / (name + '.sv')
            runner = fixtures / (name + 'Run.cc')
            run([args.hdlcpp, source])
            flags = ['-std=c++23', '-I' + str(include), '-I' + str(work)]
            output = work / name
            if args.cpphdl:
                seed = work / (name + '.cc')
                instance = name + ('<>' if name == 'ByteStringContexts' else '')
                seed.write_text(f'#include "generated/{name}.h"\nextern {instance} cpphdl_top;\n')
                run([args.cpphdl, '--native-graph', '--top', 'cpphdl_top', '--cxx', args.cxx,
                     '--frontend-flag=-I' + str(work), '--output', output, '--runner', runner,
                     seed, '--', *flags, '-DSTRING_GRAPH', '-fsanitize=address,undefined'])
                print(run([output / 'run']), end='', flush=True)
            elif args.verilator:
                run([args.verilator, '--cc', '--exe', '--build', '-j', '1', '-Wno-fatal',
                     '--top-module', name, '--Mdir', output, '-CFLAGS',
                     ' '.join(flags + ['-DSTRING_RTL']), source, runner])
                print(run([output / ('V' + name)]), end='', flush=True)
            else:
                for optimization in ('-O0', '-O2'):
                    run([args.cxx, *flags, optimization, '-fsanitize=address,undefined',
                         runner, '-o', output])
                    print(run([output]), end='', flush=True)


if __name__ == '__main__':
    main()
