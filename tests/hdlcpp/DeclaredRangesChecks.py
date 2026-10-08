"""Declared packed ranges retain inclusive signed bounds at every specialization."""
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
    fixture = Path(__file__).resolve().parent
    include = fixture.parents[1] / 'include'
    args.work.mkdir(parents=True, exist_ok=True)
    work = Path(tempfile.mkdtemp(prefix='declared-ranges-', dir=args.work))
    toolchain = TestToolchain(args.cxx, args.verilator, work)

    def run(command, label):
        result = subprocess.run(toolchain.command(command), cwd=work, env=toolchain.env, text=True,
                                stdout=subprocess.PIPE, stderr=subprocess.STDOUT, timeout=180)
        (work / (label + '.log')).write_text(result.stdout)
        if result.returncode:
            raise RuntimeError(f'{label}: {result.stdout[-6000:]}\nArtifacts: {work}')
        return result.stdout

    run([args.hdlcpp, fixture / 'DeclaredRanges.sv'], 'convert')
    runner = fixture / 'DeclaredRangesRun.cc'
    for depth, left, right in ((1, -3, 1), (2, 1, -3), (3, -5, -2), (5, -2, -5), (1, 0, 0)):
        label = f'{depth}-{left}-{right}'
        definitions = [f'-DTEST_DEPTH={depth}', f'-DTEST_LEFT={left}', f'-DTEST_RIGHT={right}']
        flags = ['-std=c++23', '-I' + str(include), '-I' + str(work), *definitions]
        output = work / label
        if args.cpphdl:
            seed = work / (label + '.cc')
            seed.write_text('#include "generated/DeclaredRanges.h"\n'
                            'extern DeclaredRanges<TEST_DEPTH, TEST_LEFT, TEST_RIGHT> cpphdl_top;\n')
            run([args.cpphdl, '--native-graph', '--top', 'cpphdl_top', '--cxx', args.cxx,
                 '--frontend-flag=-I' + str(work), *['--frontend-flag=' + flag for flag in definitions],
                 '--runner', runner, '--output', output, seed, '--', *flags,
                 '-DRANGE_GRAPH', '-fsanitize=address,undefined'], label + '-build')
            print(run([output / 'run'], label + '-run'), end='')
        elif args.verilator:
            run([args.verilator, '--cc', '--exe', '--build', '-j', '1', '-Wno-fatal',
                 '--top-module', 'DeclaredRanges', '--Mdir', output, f'-GDEPTH={depth}',
                 f'-GLEFT={left}', f'-GRIGHT={right}', '-CFLAGS', ' '.join(flags + ['-DRANGE_VERILATOR']),
                 fixture / 'DeclaredRanges.sv', runner], label + '-build')
            print(run([output / 'VDeclaredRanges'], label + '-run'), end='')
        else:
            for optimization in ('-O0', '-O2'):
                run([args.cxx, *flags, optimization, '-fsanitize=address,undefined', runner,
                     '-o', output], label + optimization + '-build')
                print(run([output], label + optimization + '-run'), end='')


if __name__ == '__main__':
    main()
