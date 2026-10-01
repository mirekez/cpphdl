"""Keep slice indices readable without losing C++ promotion/wrap semantics."""
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
    work = Path(tempfile.mkdtemp(prefix='slice-casts-', dir=args.work))

    def run(command, label):
        result = subprocess.run(list(map(str, command)), cwd=work, text=True,
                                stdout=subprocess.PIPE, stderr=subprocess.STDOUT, timeout=180)
        (work / (label + '.log')).write_text(result.stdout)
        if result.returncode:
            raise RuntimeError(f'{label}: {result.stdout[-8000:]}\nArtifacts: {work}')
        return result.stdout

    generated = work / 'rtl'
    run([args.cpphdl, '--generated-dir=' + str(generated), fixture / 'SliceCasts.cc',
         '--', '-std=c++23', '-I' + str(include)], 'convert')
    sv = ''.join((generated / 'SliceCasts.sv').read_text().split())
    for expected in ("data_in[lane*'h20+:32]", "data_in[lane*32'h10+:16]",
                     "data_in[signed'(32'(lane))*'h10+:16]",
                     "edited_comb[lane*'h10+:16]="):
        if expected not in sv:
            raise AssertionError(f'redundant slice-index casts: expected {expected}\nArtifacts: {work}')
    # Explicit conversion still evaluates the addition at 32 bits before widening.
    if "64'(32'(" not in sv:
        raise AssertionError(f'lost explicit C++ wrap boundary\nArtifacts: {work}')
    runner = fixture / 'SliceCastsRun.cc'
    if args.verilator:
        run([args.verilator, '--cc', '--exe', '--build', '-j', '1', '-Wno-fatal',
             '--top-module', 'SliceCasts', '--Mdir', work / 'obj',
             '-CFLAGS', '-std=c++23 -DSLICE_CASTS_RTL -I' + str(include),
             generated / 'Predef_pkg.sv', generated / 'SliceCastWord_pkg.sv',
             generated / 'SliceCasts.sv', runner], 'verilator')
        executable = work / 'obj/VSliceCasts'
    else:
        executable = work / 'run'
        run([args.cxx, '-std=c++23', '-O1', '-fsanitize=address,undefined',
             '-I' + str(include), runner, '-o', executable], 'compile')
    print(run([executable], 'run'), end='')


if __name__ == '__main__':
    main()
