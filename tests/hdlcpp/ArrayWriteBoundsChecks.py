"""SV array writes ignore invalid indices without forming invalid C++ references."""
import argparse
import os
from pathlib import Path
import subprocess
import tempfile


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--hdlcpp', required=True)
    parser.add_argument('--cxx', required=True)
    parser.add_argument('--work', required=True, type=Path)
    parser.add_argument('--cpphdl')
    parser.add_argument('--verilator')
    args = parser.parse_args()
    fixture = Path(__file__).resolve().parent
    include = fixture.parents[1] / 'include'
    args.work.mkdir(parents=True, exist_ok=True)
    with tempfile.TemporaryDirectory(prefix='array-write-bounds-', dir=args.work) as temporary:
        work = Path(temporary)

        def run(command, env=None):
            result = subprocess.run(list(map(str, command)), cwd=work, env=env, text=True,
                                    stdout=subprocess.PIPE, stderr=subprocess.STDOUT, timeout=180)
            if result.returncode:
                logs = ''.join(path.read_text(errors='replace')[-6000:]
                               for path in work.glob('*/cpp-to-graph.log'))
                raise RuntimeError(f'{command}\n{result.stdout[-6000:]}\n{logs}')
            return result.stdout

        seed = work / 'seed.cc'
        seed.write_text('#include "generated/ArrayWriteBounds.h"\n'
                        'extern ArrayWriteBounds<TEST_COUNT, TEST_LOWER, TEST_ZERO> cpphdl_top;\n')
        for count, lower in ((8, 0), (3, 4)):
            defines = ['-DBOUNDS_UNPACKED'] if lower else []
            run([args.hdlcpp, fixture / 'ArrayWriteBounds.sv'],
                dict(os.environ, HDLCPP_DEFINES='BOUNDS_UNPACKED' if lower else ''))
            for constant in (0, 1):
                output = work / f'{count}-{lower}-{constant}'
                definitions = [f'-DTEST_COUNT={count}', f'-DTEST_LOWER={lower}', f'-DTEST_ZERO={constant}']
                flags = ['-std=c++23', '-I' + str(include), '-I' + str(work), *definitions]
                runner = fixture / 'ArrayWriteBoundsRun.cc'
                if args.cpphdl:
                    run([args.cpphdl, '--native-graph', '--top', 'cpphdl_top', '--cxx', args.cxx,
                         *['--frontend-flag=' + flag for flag in definitions],
                         '--runner', runner, '--output', output, seed, '--', *flags,
                         '-DBOUNDS_GRAPH', '-fsanitize=address,undefined'])
                    print(run([output / 'run']), end='', flush=True)
                elif args.verilator:
                    run([args.verilator, '--cc', '--exe', '--build', '-j', '1', '-Wno-fatal',
                         '--top-module', 'ArrayWriteBounds', '--Mdir', output, f'-GCOUNT={count}',
                         f'-GLOWER={lower}', f'-GZERO_ID={constant}', '-CFLAGS',
                         ' '.join(flags + ['-DBOUNDS_RTL']), '-DBOUNDS_RTL_SAFE_INDEX',
                         *defines, fixture / 'ArrayWriteBounds.sv', runner])
                    print(run([output / 'VArrayWriteBounds']), end='', flush=True)
                else:
                    for optimization in ('-O0', '-O2'):
                        run([args.cxx, *flags, optimization, '-fsanitize=address,undefined',
                             runner, '-o', output])
                        print(optimization, run([output]), end='', flush=True)


if __name__ == '__main__':
    main()
