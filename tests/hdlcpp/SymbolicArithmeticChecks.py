import argparse
from pathlib import Path
import subprocess
import tempfile


def main():
    parser = argparse.ArgumentParser()
    for name in ('hdlcpp', 'cpphdl', 'cxx', 'verilator'):
        parser.add_argument('--' + name, required=True)
    parser.add_argument('--work', type=Path, required=True)
    args = parser.parse_args()
    args.work.mkdir(parents=True, exist_ok=True)
    fixtures = Path(__file__).resolve().parent
    with tempfile.TemporaryDirectory(prefix='symbolic-arithmetic-', dir=args.work) as directory:
        work = Path(directory)

        def run(command):
            result = subprocess.run([str(item) for item in command], cwd=work,
                                    capture_output=True, text=True, timeout=180)
            if result.returncode:
                logs = ''.join(path.read_text()[-3000:] for path in work.rglob('*.log'))
                raise AssertionError((command, result.stdout, result.stderr, logs))
            print(result.stdout, end='', flush=True)

        rtl = fixtures / 'SymbolicArithmetic.sv'
        runner = fixtures / 'SymbolicArithmeticRun.cc'
        run([args.hdlcpp, rtl])
        for width in (8, 64, 65):
            flags = ['-std=c++23', '-O1', '-I' + str(fixtures.parents[1] / 'include'),
                     '-I' + str(work), '-DSYMBOLIC_WIDTH=' + str(width)]
            run([args.cxx, *flags, '-fsanitize=address,undefined', runner, '-o', work / 'ordinary'])
            run([work / 'ordinary'])
            seed = work / 'seed.cc'
            seed.write_text('#include "generated/SymbolicArithmetic.h"\n'
                            f'extern SymbolicArithmetic<{width}> cpphdl_top;\n')
            output = work / ('graph-' + str(width))
            run([args.cpphdl, '--native-graph', '--top', 'cpphdl_top', '--chunk-size=2',
                 '--cxx', args.cxx, '--frontend-flag=-I' + str(work), '--output', output,
                 '--runner', runner, seed, '--', *flags, '-DSYMBOLIC_GRAPH',
                 '-fsanitize=address,undefined'])
            run([output / 'run'])
            output = work / ('verilator-' + str(width))
            run([args.verilator, '--cc', '--exe', '--build', '-j', '1', '-Wno-fatal',
                 '--top-module', 'SymbolicArithmetic', '-GWidth=' + str(width), '--Mdir', output,
                 '-CFLAGS', ' '.join(flags + ['-DSYMBOLIC_RTL']),
                 '-MAKEFLAGS', 'CXX=' + args.cxx, rtl, runner])
            run([output / 'VSymbolicArithmetic'])


if __name__ == '__main__':
    main()
