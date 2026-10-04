"""Compare narrow/ wide, fixed/ dynamic slice reads against a bitwise oracle."""
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
    parser.add_argument('--verilator')
    args = parser.parse_args()
    fixture = Path(__file__).resolve().parent
    include = fixture.parents[1] / 'include'
    args.work.mkdir(parents=True, exist_ok=True)
    work = Path(tempfile.mkdtemp(prefix='wide-slice-', dir=args.work))
    toolchain = TestToolchain(args.cxx, getattr(args, 'verilator', None), work)

    def run(command, label, expected_failure=False):
        result = subprocess.run(toolchain.command(command), cwd=work, env=toolchain.env, text=True,
                                stdout=subprocess.PIPE, stderr=subprocess.STDOUT, timeout=180)
        (work / (label + '.log')).write_text(result.stdout)
        if (result.returncode == 0) == expected_failure:
            logs = '\n'.join(str(p) + '\n' + p.read_text()[-4000:]
                             for p in work.glob('*/cpp-to-graph.log'))
            raise RuntimeError(f'{label}: {result.stdout[-6000:]}\n{logs}\nArtifacts: {work}')
        return result.stdout

    source = fixture / 'WideSliceMatrix.cc'
    runner = fixture / 'WideSliceMatrixRun.cc'
    if args.verilator:
        generated = work / 'rtl'
        run([args.cpphdl, '--generated-dir=' + str(generated), source,
             '--', '-std=c++23', '-I' + str(include)], 'convert')
    for line, select, fixed in ((128, 32, 0), (128, 32, 1), (64, 32, 0),
                                (32, 8, 0), (65, 1, 0), (193, 65, 0), (257, 96, 0)):
        label = f'{line}-{select}-{fixed}'
        definitions = [f'-DWIDE_LINE={line}', f'-DWIDE_SELECT={select}', f'-DWIDE_FIXED={fixed}']
        output = work / label
        if args.verilator:
            run([args.verilator, '--cc', '--exe', '--build', '-j', '1', '-Wno-fatal',
                 '--top-module', 'WideSliceMatrix', '--Mdir', output,
                 f'-GLineWidth={line}', f'-GSelectWidth={select}', f'-GFixed={fixed}',
                 '-CFLAGS', ' '.join(['-std=c++23', '-DWIDE_RTL', *definitions, '-I' + str(include)]),
                 generated / 'Predef_pkg.sv', generated / 'WideSliceMatrix.sv', runner], label + '-build')
            executable = output / 'VWideSliceMatrix'
        else:
            # Native C++ must pass independently even when lowering fails.
            native = work / ('native-' + label)
            run([args.cxx, '-std=c++23', '-O1', '-fsanitize=address,undefined', *definitions,
                 '-I' + str(include), runner, '-o', native], label + '-native-build')
            print(run([native], label + '-native-run'), end='')
            run([args.cpphdl, '--native-graph', '--top', 'cpphdl_top', '--cxx', args.cxx,
                 *['--frontend-flag=' + flag for flag in definitions], '--runner', runner,
                 '--output', output, source, '--', '-I' + str(include), '-DWIDE_GRAPH',
                 *definitions, '-fsanitize=address,undefined'], label + '-graph')
            executable = output / 'run'
        print(run([executable], label + '-run'), end='')

    if not args.verilator:
        rejected = work / 'wide-write.graph.cc'
        diagnostic = run([args.cpphdl, '--lower-cpp-graph',
                          fixture / 'WideSliceWriteReject.cc', rejected, 'cpphdl_top',
                          '--', '-std=c++23', '-I' + str(include)],
                         'wide-write-reject', expected_failure=True)
        assert 'direct current-state C++ mutation unsupported:' in diagnostic, diagnostic
        assert not rejected.exists(), 'invalid current-state write emitted a graph'


if __name__ == '__main__':
    main()
