"""Algebraically constant bits() widths must remain legal SV part-selects."""
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
    work = Path(tempfile.mkdtemp(prefix='slice-widths-', dir=args.work))

    def run(command, label):
        result = subprocess.run(list(map(str, command)), cwd=work, text=True,
                                stdout=subprocess.PIPE, stderr=subprocess.STDOUT, timeout=180)
        (work / (label + '.log')).write_text(result.stdout)
        if result.returncode:
            raise RuntimeError(f'{label}: {result.stdout[-8000:]}\nArtifacts: {work}')
        return result.stdout

    generated = work / 'rtl'
    run([args.cpphdl, '--generated-dir=' + str(generated), fixture / 'SliceWidths.cc',
         '--', '-std=c++23', '-I' + str(include)], 'convert')
    runner = fixture / 'SliceWidthsRun.cc'
    for width in (32, 16, 7):
        definitions = [f'-DSLICE_WIDTH={width}']
        if args.verilator:
            obj = work / f'obj-{width}'
            run([args.verilator, '--cc', '--exe', '--build', '-j', '1', '-Wno-fatal',
                 '--top-module', 'SliceWidths', '--Mdir', obj, f'-GWIDTH={width}',
                 '-CFLAGS', ' '.join(['-std=c++23', '-DSLICE_WIDTHS_RTL', *definitions,
                                      '-I' + str(include)]),
                 generated / 'Predef_pkg.sv', generated / 'SliceWidthWord_pkg.sv',
                 generated / 'SliceWidths.sv', runner], f'verilator-{width}')
            executable = obj / 'VSliceWidths'
        else:
            executable = work / f'run-{width}'
            run([args.cxx, '-std=c++23', '-O1', '-fsanitize=address,undefined', *definitions,
                 '-I' + str(include), runner, '-o', executable], f'compile-{width}')
        print(run([executable], f'run-{width}'), end='')
    sv = ''.join((generated / 'SliceWidths.sv').read_text().split())
    for select in sv.split('+:')[1:]:
        width = select.split(']', 1)[0]
        if width not in ('32', '16', 'WIDTH'):
            raise AssertionError(f'nonconstant/unsimplified part-select width: {width}\nArtifacts: {work}')
    if '+:WIDTH]' not in sv:
        raise AssertionError('lost symbolic module width')


if __name__ == '__main__':
    main()
