import argparse
from pathlib import Path
import subprocess
import tempfile


def main():
    parser = argparse.ArgumentParser()
    for name in ('cpphdl', 'cxx'):
        parser.add_argument('--' + name, required=True)
    parser.add_argument('--work', type=Path, required=True)
    args = parser.parse_args()
    args.work.mkdir(parents=True, exist_ok=True)
    fixtures = Path(__file__).resolve().parent
    include = fixtures.parents[1] / 'include'
    source = fixtures / 'CppGraphFinalPruning.cc'
    with tempfile.TemporaryDirectory(prefix='final-pruning-', dir=args.work) as directory:
        work = Path(directory)

        def run(command, success=True):
            result = subprocess.run([str(item) for item in command], cwd=work,
                                    capture_output=True, text=True, timeout=180)
            if (result.returncode == 0) != success:
                logs = ''.join(path.read_text()[-3000:] for path in work.rglob('*.log'))
                raise AssertionError((command, result.stdout, result.stderr, logs))
            print(result.stdout, end='', flush=True)
            return result.stdout + result.stderr

        partition = work / 'partition.graph'
        diagnostic = run([args.cpphdl, '--lower-cpp-graph', source, partition, 'cpphdl_top',
                          '--', '-std=c++23', '-I' + str(include)], success=False)
        assert 'undriven bit' in diagnostic
        assert not partition.exists()
        for effects in (0, 1):
            flags = ['-std=c++23', '-O1', '-I' + str(include), '-DFINAL_PRUNING_RUN',
                     '-DPRUNE_EFFECT=' + str(effects), '-fsanitize=address,undefined']
            run([args.cxx, *flags, source, '-o', work / 'ordinary'])
            run([work / 'ordinary'])
            for chunk in (0, 2):
                output = work / f'graph-{effects}-{chunk}'
                run([args.cpphdl, '--native-graph', '--top', 'cpphdl_top', '--chunk-size=' + str(chunk),
                     '--cxx', args.cxx, '--frontend-flag=-DPRUNE_EFFECT=' + str(effects),
                     '--output', output, '--runner', source, source, '--', *flags, '-DFINAL_PRUNING_GRAPH'])
                run([output / 'run'])
                assert 'hidden.state' not in (output / 'model.h').read_text()
        for name, flag in (('observable', 'PRUNE_OBSERVE=1'),
                           ('host-dependent', 'PRUNE_EFFECT=2'),
                           ('memory-write', 'PRUNE_MEMORY=1'),
                           ('memory-bounds', 'PRUNE_MEMORY=2')):
            output = work / name
            run([args.cpphdl, '--native-graph', '--top', 'cpphdl_top', '--chunk-size=2',
                 '--cxx', args.cxx, '--frontend-flag=-D' + flag,
                 '--output', output, source], success=False)
            diagnostic = (output / 'cpp-to-graph.log').read_text()
            assert 'undriven bit' in diagnostic, (name, diagnostic)
            assert not (output / 'model.h').exists()
        print('partition, output, host-effect, memory-write and bounds liveness preserved: PASS', flush=True)


if __name__ == '__main__':
    main()
