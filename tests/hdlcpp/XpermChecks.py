"""Conditional runtime selects must have compatible C++ branch types."""
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
    parser.add_argument('--work', type=Path, required=True)
    parser.add_argument('--verilator')
    parser.add_argument('--cpphdl')
    args = parser.parse_args()
    fixture = Path(__file__).resolve().parent
    include = fixture.parents[1] / 'include'
    args.work.mkdir(parents=True, exist_ok=True)
    work = Path(tempfile.mkdtemp(prefix='xperm-', dir=args.work))
    toolchain = TestToolchain(args.cxx, args.verilator, work)

    def run(command, label):
        result = subprocess.run(toolchain.command(command), cwd=work, env=toolchain.env, text=True,
                                stdout=subprocess.PIPE, stderr=subprocess.STDOUT, timeout=180)
        (work / (label + '.log')).write_text(result.stdout)
        if result.returncode:
            raise RuntimeError(f'{label}: {result.stdout[-8000:]}\nArtifacts: {work}')
        return result.stdout

    run([args.hdlcpp, fixture / 'Xperm.sv'], 'convert')
    generated = (work / 'generated/Xperm.h').read_text()
    if 'sv_bits_runtime' in generated or '.bits(' not in generated:
        raise RuntimeError('Xperm must use public bit slices without the runtime helper')
    for width in (32, 64):
        flags = ['-std=c++23', '-I' + str(include), '-I' + str(work), '-DTEST_XLEN=' + str(width)]
        if args.cpphdl:
            seed = work / f'seed-{width}.cc'
            seed.write_text('#include "generated/Xperm.h"\nextern Xperm<TEST_XLEN> cpphdl_top;\n')
            output = work / f'graph-{width}'
            run([args.cpphdl, '--native-graph', '--top', 'cpphdl_top', '--cxx', args.cxx,
                 '--frontend-flag=-I' + str(work), '--frontend-flag=-DTEST_XLEN=' + str(width),
                 '--runner', fixture / 'XpermRun.cc', '--output', output, seed,
                 '--', *flags, '-DXPERM_GRAPH', '-fsanitize=address,undefined'], f'graph-{width}')
            print(run([output / 'run'], f'graph-run-{width}'), end='')
        elif not args.verilator:
            for optimization in ('-O0', '-O2'):
                name = f'cpp-{width}-{optimization}'
                run([args.cxx, *flags, optimization, '-fsanitize=address,undefined',
                     fixture / 'XpermRun.cc', '-o', work / name], name + '-compile')
                print(run([work / name], name), end='')
        else:
            run([args.verilator, '--cc', '--exe', '--build', '-j', '1', '-Wno-fatal',
                 '--top-module', 'Xperm', '-GXLEN=' + str(width), '--Mdir', work / f'obj-{width}',
                 '-CFLAGS', ' '.join(flags + ['-DXPERM_VERILATOR']),
                 fixture / 'Xperm.sv', fixture / 'XpermRun.cc'], f'verilator-{width}')
            print(run([work / f'obj-{width}/VXperm'], f'run-{width}'), end='')


if __name__ == '__main__':
    main()
