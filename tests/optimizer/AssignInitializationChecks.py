"""Procedural setup remains in the original _assign, not the comb schedule."""

import argparse
from pathlib import Path
import subprocess
import tempfile


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--cpphdl', required=True)
    parser.add_argument('--cxx', required=True)
    args = parser.parse_args()
    fixture = Path(__file__).resolve().parent
    include = fixture.parents[1] / 'include'
    source = fixture / 'AssignInitializationRun.cc'
    common = ['-std=c++23', '-w', '-I' + str(include), '-I' + str(fixture)]

    def run(command, success=True):
        result = subprocess.run(list(map(str, command)), text=True,
                                stdout=subprocess.PIPE, stderr=subprocess.STDOUT, timeout=90)
        if (result.returncode == 0) != success:
            raise RuntimeError(f'{command}:\n{result.stdout[-6000:]}')
        return result.stdout

    with tempfile.TemporaryDirectory(prefix='combs-assign-', dir='/tmp') as temporary:
        work = Path(temporary)
        ordinary = work / 'ordinary'
        run([args.cxx, *common, '-O2', source, '-o', ordinary])
        expected = run([ordinary])
        for mode in ('--optimize-combs', '--optimize-combs-l1'):
            output = work / mode.lstrip('-')
            collection = work / (output.name + '.bin')
            for collected in (False, True):
                loads = []
                if collected:
                    run([args.cpphdl, mode, 'AssignInitialization',
                         '--optimize-combs-collect=' + str(collection), source, '--', *common])
                    loads = ['--optimize-combs-load=' + str(collection)]
                run([args.cpphdl, mode, 'AssignInitialization', *loads,
                     '--generated-dir=' + str(output), source, '--', *common])
                for optimization in ('-O0', '-O2'):
                    executable = output / 'run'
                    run([args.cxx, *common, optimization, '-DASSIGN_OPTIMIZED',
                         '-I' + str(output), source, *sorted(output.glob('*.cpp')), '-o', executable])
                    assert run([executable]) == expected
        for rejected in (1, 2):
            diagnostic = run([args.cpphdl, '--optimize-combs', 'AssignInitialization',
                              '--generated-dir=' + str(work / f'reject-{rejected}'), fixture / 'AssignInitialization.h',
                              '--', *common, f'-DASSIGN_REJECT={rejected}'], success=False)
            assert 'unsupported statement in _assign()' in diagnostic, diagnostic
        print(expected, end='')
        print('PASS: direct/collected, normal/L1, O0/O2; unsupported bindings still rejected')


if __name__ == '__main__':
    main()
