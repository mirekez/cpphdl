"""Local reg copies retain current/next values without creating graph state."""
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
    work = Path(tempfile.mkdtemp(prefix='local-reg-', dir=args.work))

    def run(command, label, success=True):
        result = subprocess.run(list(map(str, command)), cwd=work, text=True,
                                stdout=subprocess.PIPE, stderr=subprocess.STDOUT, timeout=180)
        (work / (label + '.log')).write_text(result.stdout)
        if (result.returncode == 0) != success:
            logs = '\n'.join(p.read_text()[-3000:] for p in work.glob('*/cpp-to-graph.log'))
            raise RuntimeError(f'{label}: {result.stdout[-4000:]}\n{logs}\nArtifacts: {work}')
        return result.stdout

    for lanes, control in ((1, 0), (1, 1), (1, 2), (9, 3)):
        name = f'lanes-{lanes}-control-{control}'
        output = work / name
        defines = [f'-DCOPY_LANES={lanes}', f'-DCOPY_CONTROL={control}']
        common = ['-std=c++23', *defines, '-I' + str(include)]
        runner = fixture / 'LocalRegCopyRun.cc'
        if not args.verilator:
            native = work / (name + '-native')
            run([args.cxx, *common, '-O1', '-fsanitize=address,undefined', runner, '-o', native], name + '-native-build')
            print(run([native], name + '-native-run'), end='')
        command = [args.cpphdl, '--native-graph', '--top', 'cpphdl_top', '--cxx', args.cxx,
                   *['--frontend-flag=' + flag for flag in defines], '--output', output]
        if not args.verilator:
            command += ['--runner', runner]
        command += [fixture / 'LocalRegCopy.cc']
        if not args.verilator:
            command += ['--', *common, '-DREG_GRAPH', '-fsanitize=address,undefined']
        run(command, name + '-graph')
        # Only state, audit and committed_copy are hardware registers.
        diagnostic = (output / 'cpp-to-graph.log').read_text()
        assert 'registers=3 ' in diagnostic, diagnostic
        executable = output / 'run'
        if args.verilator:
            run([args.verilator, '--cc', '--exe', '--build', '-j', '1', '-Wno-fatal',
                 '--top-module', 'LocalRegCopy', '--Mdir', output / 'obj', f'-GLANES={lanes}',
                 '-CFLAGS', ' '.join([*common, '-DREG_GRAPH', '-DREG_RTL', '-I' + str(output)]),
                 fixture / 'LocalRegCopy.sv', runner], name + '-rtl-build')
            executable = output / 'obj/VLocalRegCopy'
        print(run([executable], name + '-run'), end='')

    if not args.verilator:
        for case in range(5):
            output = work / f'reject-{case}.cc'
            diagnostic = run([args.cpphdl, '--lower-cpp-graph', fixture / 'LocalRegCopyReject.cc',
                              output, 'cpphdl_top', '--', '-std=c++23', '-I' + str(include),
                              f'-DREG_REJECT={case}'], f'reject-{case}', success=False)
            assert 'direct current-state C++ mutation unsupported:' in diagnostic, diagnostic
            assert not output.exists(), 'invalid mutation emitted a graph'
        print('direct register writes and writes through references remain rejected')


if __name__ == '__main__':
    main()
