"""Collect dependent port initializers without instantiating constructors."""

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
    seed = fixture / 'TemplatePortInitializerSeed.cc'
    common = ['-std=c++23', '-w', '-I' + str(include), '-I' + str(fixture)]

    def run(command):
        result = subprocess.run(list(map(str, command)), text=True,
                                stdout=subprocess.PIPE, stderr=subprocess.STDOUT, timeout=90)
        if result.returncode:
            raise RuntimeError(f'{command}:\n{result.stdout[-6000:]}')

    with tempfile.TemporaryDirectory(prefix='combs-template-port-', dir='/tmp') as temporary:
        work = Path(temporary)
        for mode in ('--optimize-combs', '--optimize-combs-l1'):
            for collected in (False, True):
                output = work / f'{mode.lstrip("-")}-{collected}'
                collection = work / (output.name + '.bin')
                loads = []
                if collected:
                    run([args.cpphdl, mode, 'TemplatePortInitializer',
                         '--optimize-combs-collect=' + str(collection), seed, '--', *common])
                    loads = ['--optimize-combs-load=' + str(collection)]
                run([args.cpphdl, mode, 'TemplatePortInitializer', *loads,
                     '--generated-dir=' + str(output), seed, '--', *common])
                for optimization in ('-O0', '-O2'):
                    executable = output / 'run'
                    run([args.cxx, *common, optimization, '-I' + str(output),
                         fixture / 'TemplatePortInitializerRun.cc',
                         *sorted(output.glob('*.cpp')), '-o', executable])
                    run([executable])
        print('PASS: initialized dependent ports; direct/collected, normal/L1, O0/O2')


if __name__ == '__main__':
    main()
