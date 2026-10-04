"""Empty scalar conversions: native C++, graph, and original-SV RTL oracle."""
import argparse
from pathlib import Path
import subprocess
import tempfile


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
    work = Path(tempfile.mkdtemp(prefix='empty-repeat-', dir=args.work))

    def run(command, label, success=True):
        result = subprocess.run(list(map(str, command)), cwd=work, text=True,
                                stdout=subprocess.PIPE, stderr=subprocess.STDOUT, timeout=180)
        (work / (label + '.log')).write_text(result.stdout)
        if (result.returncode == 0) != success:
            logs = '\n'.join(p.read_text()[-3000:] for p in work.glob('*/cpp-to-graph.log'))
            raise RuntimeError(f'{label}: {result.stdout[-4000:]}\n{logs}\nArtifacts: {work}')
        return result.stdout

    for count in (0, 1, 8, 32):
        name = 'count-' + str(count)
        output = work / name
        define = '-DEMPTY_REPEAT_COUNT=' + str(count)
        common = ['-std=c++23', define, '-I' + str(include)]
        runner = fixture / 'EmptyRepeatCastRun.cc'
        if not args.verilator:
            native = work / (name + '-native')
            run([args.cxx, *common, '-O1', '-fsanitize=address,undefined',
                 runner, '-o', native], name + '-native-build')
            print(run([native], name + '-native-run'), end='')
        graph_command = [args.cpphdl, '--native-graph', '--top', 'cpphdl_top',
                         '--cxx', args.cxx, '--frontend-flag=' + define, '--output', output]
        if not args.verilator:
            graph_command += ['--runner', runner]
        graph_command += [fixture / 'EmptyRepeatCast.cc']
        if not args.verilator:
            graph_command += ['--', *common, '-DEMPTY_GRAPH', '-fsanitize=address,undefined']
        run(graph_command, name + '-graph')
        executable = output / 'run'
        if args.verilator:
            run([args.verilator, '--cc', '--exe', '--build', '-j', '1', '-Wno-fatal',
                 '--top-module', 'EmptyRepeatCast', '--Mdir', output / 'obj', f'-GCount={count}',
                 '-CFLAGS', ' '.join([*common, '-DEMPTY_GRAPH', '-DEMPTY_RTL', '-I' + str(output)]),
                 fixture / 'EmptyRepeatCast.sv', runner], name + '-rtl-build')
            executable = output / 'obj/VEmptyRepeatCast'
        print(run([executable], name + '-run'), end='')

    if not args.verilator:
        for case in range(5):
            output = work / f'reject-{case}.cc'
            diagnostic = run([args.cpphdl, '--lower-cpp-graph', fixture / 'EmptyRepeatCastReject.cc',
                              output, 'cpphdl_top', '--', '-std=c++23', '-I' + str(include),
                              f'-DEMPTY_REJECT={case}'], f'reject-{case}', success=False)
            assert 'unsupported C++ hardware type:' in diagnostic, diagnostic
            assert not output.exists(), 'zero-width storage or all-empty concat emitted a graph'
        print('zero-width ports/storage and all-empty concat still rejected inside scalar casts')


if __name__ == '__main__':
    main()
