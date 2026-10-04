"""Exhaustive switch writes must not depend on the previous comb value."""
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
    parser.add_argument('--work', type=Path, required=True)
    parser.add_argument('--verilator')
    args = parser.parse_args()
    fixture = Path(__file__).resolve().parent
    include = fixture.parents[1] / 'include'
    args.work.mkdir(parents=True, exist_ok=True)
    work = Path(tempfile.mkdtemp(prefix='complete-', dir=args.work))
    toolchain = TestToolchain(args.cxx, getattr(args, 'verilator', None), work)

    def run(command, label, reject=False):
        result = subprocess.run(toolchain.command(command), cwd=work, env=toolchain.env, text=True,
                                stdout=subprocess.PIPE, stderr=subprocess.STDOUT, timeout=180)
        (work / (label + '.log')).write_text(result.stdout)
        if reject:
            log = work / label / 'lower.log'
            diagnostic = result.stdout + (log.read_text() if log.exists() else '')
            if not result.returncode or 'undriven bit:' not in diagnostic:
                raise RuntimeError(f'{label}: expected undriven rejection: {diagnostic}\n{work}')
            if (work / label / 'model.h').exists():
                raise RuntimeError(f'{label}: rejected graph emitted a model\n{work}')
        elif result.returncode:
            raise RuntimeError(f'{label}: {result.stdout[-6000:]}\nArtifacts: {work}')
        return result.stdout

    source = fixture / 'SwitchCompleteCases.cc'
    runner = fixture / 'SwitchCompleteRun.cc'
    common = ['-std=c++23', '-I' + str(include)]
    if args.verilator:
        rtl = work / 'rtl'
        run([args.cpphdl, '--generated-dir=' + str(rtl), source, '--', *common], 'convert')
        run([args.verilator, '--cc', '--exe', '--build', '-j', '1', '-Wno-fatal',
             '--top-module', 'SwitchCompleteCases', '--Mdir', work / 'obj',
             '-CFLAGS', ' '.join([*common, '-DCOMPLETE_RTL']),
             rtl / 'Predef_pkg.sv', rtl / 'SwitchProbe.sv',
             rtl / 'SwitchCompleteCases.sv', runner], 'verilator')
        executable = work / 'obj/VSwitchCompleteCases'
    else:
        native = work / 'native'
        run([args.cxx, *common, '-O1', '-fsanitize=address,undefined', runner, '-o', native], 'native-build')
        print(run([native], 'native-run'), end='')
        native.unlink()
        run([args.cpphdl, '--native-graph', '--top', 'complete_top', '--cxx', args.cxx,
             '--output', work / 'graph', '--runner', runner, source, '--', *common,
             '-DCOMPLETE_GRAPH', '-fsanitize=address,undefined'], 'graph-build')
        executable = work / 'graph/run'
        # Exercise the emission step, not just serialization of the graph.
        for variant in range(5):
            run([args.cpphdl, '--native-graph', '--top', 'cpphdl_top', '--cxx', args.cxx,
                 '--frontend-flag=-DINCOMPLETE=' + str(variant),
                 '--output', work / f'reject-{variant}', fixture / 'SwitchIncomplete.cc'],
                f'reject-{variant}', reject=True)
        print('complete switch: 5 incomplete-write variants correctly rejected')
    print(run([executable], 'run'), end='')
    if not args.verilator:
        executable.unlink()


if __name__ == '__main__':
    main()
