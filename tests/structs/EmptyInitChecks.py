"""Compare C++ aggregate value initialization with generated SystemVerilog."""
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
    work = Path(tempfile.mkdtemp(prefix='empty-init-', dir=args.work))

    def run(command, label):
        result = subprocess.run(list(map(str, command)), cwd=work, text=True,
                                stdout=subprocess.PIPE, stderr=subprocess.STDOUT, timeout=180)
        (work / (label + '.log')).write_text(result.stdout)
        if result.returncode:
            raise RuntimeError(f'{label}: {result.stdout[-8000:]}\nArtifacts: {work}')
        return result.stdout

    runner = fixture / 'EmptyInitRun.cc'
    if not args.verilator:
        executable = work / 'run'
        run([args.cxx, '-std=c++23', '-O1', '-fsanitize=address,undefined',
             '-I' + str(include), runner, '-o', executable], 'compile')
    else:
        generated = work / 'rtl'
        run([args.cpphdl, '--generated-dir=' + str(generated), fixture / 'EmptyInit.cc',
             '--', '-std=c++23', '-I' + str(include)], 'convert')
        sv = ''.join((generated / 'EmptyInit.sv').read_text().split())
        for forbidden in ('{}', 'unknown:'):
            if forbidden in sv:
                raise AssertionError(f'invalid aggregate initializer: {forbidden}\nArtifacts: {work}')
        # Dependencies must precede packages containing nested structs.
        packages = ['Predef', 'EmptyInitPlain', 'EmptyInitBits', 'EmptyInitDefaults',
                    'EmptyInitNested', 'EmptyInitNestedDefaults', 'EmptyInitDerived',
                    'EmptyInitExtended', 'EmptyInitUnion']
        run([args.verilator, '--cc', '--exe', '--build', '-j', '1', '-Wno-fatal',
             '--top-module', 'EmptyInit', '--Mdir', work / 'obj',
             '-CFLAGS', '-std=c++23 -DEMPTY_INIT_VERILATOR -I' + str(include),
             *[generated / (name + '_pkg.sv') for name in packages],
             generated / 'EmptyInit.sv', runner], 'verilator')
        executable = work / 'obj/VEmptyInit'
    print(run([executable], 'run'), end='')


if __name__ == '__main__':
    main()
