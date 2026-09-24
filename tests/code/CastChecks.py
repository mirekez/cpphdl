"""Check supported casts against RTL, and operand fallback for other casts."""
import argparse
from pathlib import Path
import subprocess
import tempfile


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument('--cpphdl', required=True)
    parser.add_argument('--cxx', required=True)
    parser.add_argument('--work', type=Path, required=True)
    parser.add_argument('--flow', choices=('cpp', 'verilator', 'fallback', 'widths'), required=True)
    parser.add_argument('--verilator')
    args = parser.parse_args()
    fixture = Path(__file__).resolve().parent
    include = fixture.parents[1] / 'include'
    args.work.mkdir(parents=True, exist_ok=True)
    work = Path(tempfile.mkdtemp(prefix=args.flow + '-', dir=args.work))

    def run(command, label):
        result = subprocess.run(list(map(str, command)), cwd=work, text=True,
                                stdout=subprocess.PIPE, stderr=subprocess.STDOUT, timeout=180)
        (work / (label + '.log')).write_text(result.stdout)
        if result.returncode:
            raise RuntimeError(f'{label} failed: {result.stdout[-8000:]}\nArtifacts: {work}')
        return result.stdout

    runner = fixture / 'CastMatrixRun.cc'
    if args.flow == 'widths':
        # One generated module, overridden at elaboration: no regenerating a
        # hardcoded specialization for each width.
        run([args.cpphdl, '--generated-dir=' + str(work / 'rtl'), fixture / 'CastWidths.cc',
             '--', '-std=c++23', '-I' + str(include)], 'convert')
        for width in (1, 9, 33):
            run([args.verilator, '--cc', '--exe', '--build', '-j', '1', '-Wno-fatal',
                 '--top-module', 'CastWidths', '-GWIDTH=' + str(width),
                 '--Mdir', work / f'obj-{width}',
                 '-CFLAGS', f'-std=c++23 -DCAST_VERILATOR -DCAST_WIDTH={width} -I{include}',
                 work / 'rtl/Predef_pkg.sv', work / 'rtl/CastWidths.sv',
                 fixture / 'CastWidthsRun.cc'], f'verilator-{width}')
            print(run([work / f'obj-{width}/VCastWidths'], f'run-{width}'), end='')
        return
    if args.flow == 'fallback':
        for case in range(8):
            # Check conversion policy, not behavioral equivalence for runtime
            # features that have no hardware lowering.
            flags = ['-std=c++23', '-I' + str(include), '-DCAST_FALLBACK=' + str(case)]
            source = fixture / 'CastFallback.cc'
            run([args.cxx, '-fsyntax-only', source, *flags], f'syntax-{case}')
            run([args.cpphdl, '--generated-dir=' + str(work / f'rtl-{case}'),
                 source, '--', *flags], f'fallback-{case}')
            sv = (work / f'rtl-{case}/CastFallback.sv').read_text()
            # The operand must survive: simply suppressing the error while
            # returning an empty expression would silently erase the write.
            operand = 'base.value' if case == 0 else 'value' if case in (3, 4, 7) else 'p'
            if not any('result=' in ''.join(line.split()) and operand in line for line in sv.splitlines()):
                raise AssertionError(f'fallback {case} lost operand {operand}: {sv}')
        print('unsupported cast operand fallback: eight cases passed')
        return
    if args.flow == 'cpp':
        run([args.cxx, '-std=c++23', '-O1', '-fsanitize=address,undefined',
             '-I' + str(include), runner, '-o', work / 'run'], 'compile')
        executable = work / 'run'
    else:
        run([args.cpphdl, '--generated-dir=' + str(work / 'rtl'), fixture / 'CastMatrix.cc',
             '--', '-std=c++23', '-I' + str(include)], 'convert')
        sv = sorted((work / 'rtl').glob('*_pkg.sv')) + [work / 'rtl/CastMatrix.sv']
        run([args.verilator, '--cc', '--exe', '--build', '-j', '1', '-Wno-fatal',
             '--top-module', 'CastMatrix', '--Mdir', work / 'obj',
             '-CFLAGS', '-std=c++23 -DCAST_VERILATOR -I' + str(include),
             *sv, runner], 'verilator')
        executable = work / 'obj/VCastMatrix'
    print(run([executable], 'run'), end='')


if __name__ == '__main__':
    main()
