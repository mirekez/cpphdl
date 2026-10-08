"""Zero-width concat temporaries are empty values, never hardware storage."""
import argparse
from pathlib import Path
import subprocess
import tempfile
import sys

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))
from TestToolchain import TestToolchain


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--cpphdl', required=True)
    parser.add_argument('--cxx', required=True)
    parser.add_argument('--work', required=True, type=Path)
    parser.add_argument('--hdlcpp')
    parser.add_argument('--verilator')
    args = parser.parse_args()
    fixture = Path(__file__).resolve().parent
    include = fixture.parents[1] / 'include'
    args.work.mkdir(parents=True, exist_ok=True)
    work = Path(tempfile.mkdtemp(prefix='zero-concat-', dir=args.work))
    toolchain = TestToolchain(args.cxx, getattr(args, 'verilator', None), work)

    def run(command, label, success=True):
        result = subprocess.run(toolchain.command(command), cwd=work, env=toolchain.env, text=True,
                                stdout=subprocess.PIPE, stderr=subprocess.STDOUT, timeout=180)
        (work / (label + '.log')).write_text(result.stdout)
        if (result.returncode == 0) != success:
            raise RuntimeError(f'{label}: {result.stdout[-6000:]}\nArtifacts: {work}')
        return result.stdout

    if args.hdlcpp:
        if not args.verilator:
            parser.error('--hdlcpp requires --verilator')
        run([args.hdlcpp, fixture / 'ZeroRepeatWidths.sv'], 'hdlcpp')
        for xlen, vlen in ((32, 32), (48, 32), (31, 31), (40, 7)):
            name = f'sv-{xlen}-{vlen}'
            output = work / name
            definitions = [f'-DZERO_XLEN={xlen}', f'-DZERO_VLEN={vlen}']
            run([args.cpphdl, '--native-graph', '--top', 'cpphdl_top', '--cxx', args.cxx,
                 '--frontend-flag=-I' + str(work),
                 *['--frontend-flag=' + flag for flag in definitions],
                 '--output', output, fixture / 'ZeroRepeatWidthsSeed.cc'], name + '-graph')
            run([args.verilator, '--cc', '--exe', '--build', '-j', '1', '-Wno-fatal',
                 '--top-module', 'ZeroRepeatWidths', '--Mdir', output / 'obj',
                 f'-GXLEN={xlen}', f'-GVLEN={vlen}',
                 '-CFLAGS', ' '.join(['-std=c++23', *definitions, '-I' + str(include),
                                      '-I' + str(work), '-I' + str(output)]),
                 fixture / 'ZeroRepeatWidths.sv', fixture / 'ZeroRepeatWidthsRun.cc'], name + '-verilator')
            print(run([output / 'obj/VZeroRepeatWidths'], name + '-run'), end='')
    else:
        run([args.cpphdl, '--native-graph', '--top', 'cpphdl_top', '--cxx', args.cxx,
             '--runner', fixture / 'ZeroConcatCasesRun.cc', '--output', work / 'direct',
             fixture / 'ZeroConcatCases.cc', '--', '-I' + str(include),
             '-fsanitize=address,undefined'], 'direct-graph')
        print(run([work / 'direct/run'], 'direct-run'), end='')
        for case in range(8):
            output = work / f'reject-{case}.cc'
            log = run([args.cpphdl, '--lower-cpp-graph', fixture / 'ZeroConcatReject.cc',
                       output, 'cpphdl_top', '--', '-std=c++23', '-I' + str(include),
                       f'-DZERO_REJECT={case}'], f'reject-{case}', success=False)
            if 'unsupported C++ hardware type:' not in log or output.exists():
                raise RuntimeError(f'case {case}: missing width diagnostic or emitted invalid graph\n{log}')
        print('zero-width ports, storage, standalone repeat and all-empty concat remain rejected')


if __name__ == '__main__':
    main()
