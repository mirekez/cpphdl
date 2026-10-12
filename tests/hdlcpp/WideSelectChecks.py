"""Wide bit selections and context-sized carry must survive ordinary and graph lowering."""
import argparse
from pathlib import Path
import subprocess
import sys
import tempfile

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
    with tempfile.TemporaryDirectory(prefix='wide-select-', dir=args.work) as temporary:
        work = Path(temporary)
        toolchain = TestToolchain(args.cxx, args.verilator, work)

        def run(command):
            result = subprocess.run(toolchain.command(command), cwd=work, env=toolchain.env,
                                    text=True, stdout=subprocess.PIPE, stderr=subprocess.STDOUT, timeout=180)
            if result.returncode:
                logs = ''.join(path.read_text(errors='replace')[-3000:]
                               for path in work.glob('*/cpp-to-graph.log'))
                raise RuntimeError(f'{command}\n{result.stdout[-6000:]}\n{logs}')
            return result.stdout

        source, runner = fixtures / 'WideSelect.sv', fixtures / 'WideSelectRun.cc'
        run([args.hdlcpp, source])
        flags = ['-std=c++23', '-I' + str(include), '-I' + str(work)]
        output = work / 'model'
        if args.cpphdl:
            seed = work / 'root.cc'
            seed.write_text('#include "generated/WideSelect.h"\nextern WideSelect<> cpphdl_top;\n')
            run([args.cpphdl, '--native-graph', '--top', 'cpphdl_top', '--chunk-size=1',
                 '--cxx', args.cxx, '--frontend-flag=-I' + str(work), '--output', output,
                 '--runner', runner, seed, '--', *flags, '-DWIDE_GRAPH', '-fsanitize=address,undefined'])
            print(run([output / 'run']), end='', flush=True)
        elif args.verilator:
            run([args.verilator, '--cc', '--exe', '--build', '-j', '1', '-Wno-fatal',
                 '--top-module', 'WideSelect', '--Mdir', output, '-CFLAGS',
                 ' '.join(flags + ['-DWIDE_RTL']), source, runner])
            print(run([output / 'VWideSelect']), end='', flush=True)
        else:
            for optimization in ('-O0', '-O2'):
                run([args.cxx, *flags, optimization, '-fsanitize=address,undefined', runner, '-o', output])
                print(run([output]), end='', flush=True)


if __name__ == '__main__':
    main()
