"""Compare overloaded unary/binary operators in C++, native graph and RTL."""
import argparse
from pathlib import Path
import subprocess
import tempfile


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
    work = Path(tempfile.mkdtemp(prefix='unary-', dir=args.work))

    def run(command, label):
        result = subprocess.run(list(map(str, command)), cwd=work, text=True,
                                stdout=subprocess.PIPE, stderr=subprocess.STDOUT, timeout=180)
        (work / (label + '.log')).write_text(result.stdout)
        if result.returncode:
            logs = '\n'.join(p.read_text()[-3000:] for p in (work / 'graph').glob('*.log'))
            raise RuntimeError(f'{label}: {result.stdout[-5000:]}\n{logs}\nArtifacts: {work}')
        return result.stdout

    source = fixture / 'UnaryCatNegation.cc'
    runner = fixture / 'UnaryCatNegationRun.cc'
    common = ['-std=c++23', '-I' + str(include)]
    if args.verilator:
        rtl = work / 'rtl'
        run([args.cpphdl, '--generated-dir=' + str(rtl), source, '--', *common], 'convert')
        run([args.verilator, '--cc', '--exe', '--build', '-j', '1', '-Wno-fatal',
             '--top-module', 'NegateProbe', '--Mdir', work / 'obj',
             '-CFLAGS', ' '.join([*common, '-DNEGATE_RTL']),
             rtl / 'Predef_pkg.sv', rtl / 'NegateProbe.sv', runner], 'verilator')
        executable = work / 'obj/VNegateProbe'
    else:
        native = work / 'native'
        run([args.cxx, *common, '-O1', '-fsanitize=address,undefined', runner, '-o', native], 'native-build')
        print(run([native], 'native-run'), end='')
        native.unlink()
        run([args.cpphdl, '--native-graph', '--top', 'cpphdl_top', '--cxx', args.cxx,
             '--output', work / 'graph', '--runner', runner, source, '--', *common,
             '-DNEGATE_GRAPH', '-fsanitize=address,undefined'], 'graph-build')
        executable = work / 'graph/run'
    print(run([executable], 'run'), end='')
    if not args.verilator:
        executable.unlink()


if __name__ == '__main__':
    main()
