"""Guard cast readability in Buffer RTL and compare its behavior with C++."""
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
    root = fixture.parents[1]
    args.work.mkdir(parents=True, exist_ok=True)
    work = Path(tempfile.mkdtemp(prefix='buffer-casts-', dir=args.work))

    def run(command, label):
        result = subprocess.run(list(map(str, command)), cwd=work, text=True,
                                stdout=subprocess.PIPE, stderr=subprocess.STDOUT, timeout=180)
        (work / (label + '.log')).write_text(result.stdout)
        if result.returncode:
            raise RuntimeError(f'{label}: {result.stdout[-6000:]}\nArtifacts: {work}')
        return result.stdout

    run([args.cpphdl, '--generated-dir=' + str(work / 'rtl'), root / 'examples/basic/Buffer.cpp',
         '--', '-std=c++23', '-I' + str(root / 'include')], 'convert')
    sv = ''.join((work / 'rtl/Buffer.sv').read_text().split())
    for expected in ('head=head_reg;', 'tail=tail_reg;', 'count=count_reg;',
                     "had_stored=count!=32'h0;"):
        if expected not in sv:
            raise AssertionError(f'redundant cast in Buffer: expected {expected}\nArtifacts: {work}')
    for duplicate in ("unsigned'(32'(unsigned'(32'(", "unsigned'(64'(unsigned'(64'("):
        if duplicate in sv:
            raise AssertionError(f'duplicate cast {duplicate}\nArtifacts: {work}')
    for expected in ("head=64'(32'(", "head_reg_tmp=INDEX_BITS'(head);"):
        if expected not in sv:
            raise AssertionError(f'Buffer width/sign cast regression: expected {expected}\nArtifacts: {work}')
    for depth in (1, 3, 8):
        flags = f'-std=c++23 -DSYNTHESIS -DBUFFER_DEPTH={depth} -I{root / "include"}'
        if args.verilator:
            obj = work / f'obj-{depth}'
            run([args.verilator, '--cc', '--exe', '--build', '-j', '1', '-Wno-fatal',
                 '--top-module', 'Buffer', '-GWIDTH=32', f'-GDEPTH={depth}', '--Mdir', obj,
                 '-CFLAGS', flags + ' -DBUFFER_RTL', work / 'rtl/Predef_pkg.sv',
                 work / 'rtl/Buffer.sv', fixture / 'BufferCastsRun.cc'], f'compile-{depth}')
            executable = obj / 'VBuffer'
        else:
            executable = work / f'run-{depth}'
            run([args.cxx, *flags.split(), '-O1', '-fsanitize=address,undefined',
                 fixture / 'BufferCastsRun.cc', '-o', executable], f'compile-{depth}')
        print(run([executable], f'run-{depth}'), end='')


if __name__ == '__main__':
    main()
