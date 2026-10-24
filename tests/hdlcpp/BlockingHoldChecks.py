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
    with tempfile.TemporaryDirectory(prefix='blocking-hold-', dir=args.work) as directory:
        work = Path(directory)

        def run(command):
            result = subprocess.run([str(item) for item in command], cwd=work,
                                    capture_output=True, text=True, timeout=180)
            if result.returncode:
                logs = ''.join(path.read_text()[-3000:] for path in work.rglob('*.log'))
                raise AssertionError((command, result.stdout, result.stderr, logs))
            print(result.stdout, end='', flush=True)

        rtl, runner = fixtures / 'BlockingHold.sv', fixtures / 'BlockingHoldRun.cc'
        run([args.hdlcpp, rtl])
        for enabled in (0, 1):
            flags = ['-std=c++23', '-O1', '-I' + str(fixtures.parents[1] / 'include'),
                     '-I' + str(work), '-DHOLD_ENABLED=' + str(enabled)]
            run([args.cxx, *flags, '-fsanitize=address,undefined', runner, '-o', work / 'ordinary'])
            run([work / 'ordinary'])
            seed = work / 'seed.cc'
            seed.write_text('#include "generated/BlockingHold.h"\n'
                            f'extern BlockingHold<{enabled}> cpphdl_top;\n')
            for chunk in (0, 2):
                output = work / f'graph-{enabled}-{chunk}'
                run([args.cpphdl, '--native-graph', '--top', 'cpphdl_top', '--chunk-size=' + str(chunk),
                     '--cxx', args.cxx, '--frontend-flag=-I' + str(work), '--output', output,
                     '--runner', runner, seed, '--', *flags, '-DHOLD_GRAPH',
                     '-fsanitize=address,undefined'])
                run([output / 'run'])
            output = work / ('verilator-' + str(enabled))
            run([args.verilator, '--cc', '--exe', '--build', '-j', '1', '-Wno-fatal',
                 '--top-module', 'BlockingHold', '-GEnabled=' + str(enabled), '--Mdir', output,
                 '-CFLAGS', ' '.join(flags + ['-DHOLD_RTL']),
                 '-MAKEFLAGS', 'CXX=' + args.cxx, rtl, runner])
            run([output / 'VBlockingHold'])


if __name__ == '__main__':
    main()
