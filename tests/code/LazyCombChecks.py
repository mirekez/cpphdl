"""Keep lazy caching in C++ simulation and out of synthesized RTL."""
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
    work = Path(tempfile.mkdtemp(prefix='lazy-', dir=args.work))

    def run(command, label):
        result = subprocess.run(list(map(str, command)), cwd=work, text=True,
                                stdout=subprocess.PIPE, stderr=subprocess.STDOUT, timeout=180)
        (work / (label + '.log')).write_text(result.stdout)
        if result.returncode:
            raise RuntimeError(f'{label}: {result.stdout[-6000:]}\nArtifacts: {work}')

    runner = fixture / 'LazyCombRun.cc'
    if not args.verilator:
        for static in (False, True):
            for synthesis in (False, True):
                name = f'static-{static}-synthesis-{synthesis}'
                flags = (['-DCPPHDL_STATIC'] if static else []) + (['-DSYNTHESIS'] if synthesis else [])
                run([args.cxx, '-std=c++23', '-O1', '-I' + str(include), *flags,
                     runner, '-o', work / name], name + '-compile')
                run([work / name], name)
        print('lazy comb C++: caching and synthesis exclusion passed in both macro variants')
        return

    generated = work / 'rtl'
    # Exercise the converter's normal implicit SYNTHESIS flag.
    run([args.cpphdl, '--generated-dir=' + str(generated), fixture / 'LazyComb.cc',
         '--', '-std=c++23', '-I' + str(include)], 'convert')
    sv = (generated / 'LazyComb.sv').read_text()
    for forbidden in ('__prev__system_clock', '$time', 'evaluations', 'disable value_comb_func'):
        if forbidden in sv:
            raise AssertionError(f'cache bookkeeping leaked into RTL: {forbidden}')
    run([args.verilator, '--cc', '--exe', '--build', '-j', '1', '-Wno-fatal',
         '--top-module', 'LazyComb', '--Mdir', work / 'obj',
         '-CFLAGS', '-std=c++23 -DLAZY_VERILATOR -I' + str(include),
         generated / 'Predef_pkg.sv', generated / 'LazyComb.sv', runner], 'verilator')
    run([work / 'obj/VLazyComb'], 'run')
    print('lazy comb RTL: no cache bookkeeping, 1024 C++/Verilator comparisons passed')


if __name__ == '__main__':
    main()
