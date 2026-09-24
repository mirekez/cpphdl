"""Port bindings must convert to the declared payload before graph operations."""
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
    work = Path(tempfile.mkdtemp(prefix='port-binding-', dir=args.work))

    def run(command, label):
        result = subprocess.run(list(map(str, command)), cwd=work, text=True,
                                stdout=subprocess.PIPE, stderr=subprocess.STDOUT, timeout=180)
        (work / (label + '.log')).write_text(result.stdout)
        if result.returncode:
            raise RuntimeError(f'{label}: {result.stdout[-8000:]}\nArtifacts: {work}')
        return result.stdout

    source = fixture / 'PortBinding.cc'
    runner = fixture / 'PortBindingRun.cc'
    if args.verilator:
        generated = work / 'rtl'
        run([args.cpphdl, '--generated-dir=' + str(generated), source,
             '--', '-std=c++23', '-I' + str(include)], 'convert')
        run([args.verilator, '--cc', '--exe', '--build', '-j', '1', '-Wno-fatal',
             '--top-module', 'PortBinding', '--Mdir', work / 'obj',
             '-CFLAGS', '-std=c++23 -DPORT_BINDING_RTL -I' + str(include),
             generated / 'Predef_pkg.sv', generated / 'PortBindingChild.sv',
             generated / 'PortBinding.sv', runner], 'verilator')
        print(run([work / 'obj/VPortBinding'], 'rtl-run'), end='')
    else:
        run([args.cxx, '-std=c++23', '-O1', '-fsanitize=address,undefined',
             '-I' + str(include), runner, '-o', work / 'native'], 'native-build')
        print(run([work / 'native'], 'native-run'), end='')
        for name, definitions in (('integer', []), ('typed', ['-DPORT_BINDING_TYPED'])):
            output = work / name
            run([args.cpphdl, '--native-graph', '--top', 'cpphdl_top', '--cxx', args.cxx,
                 *['--frontend-flag=' + flag for flag in definitions],
                 '--runner', runner, '--output', output, source, '--',
                 '-I' + str(include), '-DPORT_BINDING_GRAPH', *definitions,
                 '-fsanitize=address,undefined'], name + '-graph')
            print(run([output / 'run'], name + '-run'), end='')


if __name__ == '__main__':
    main()
