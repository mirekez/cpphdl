"""Nested packed fields preserve widths, field access, packing and unpacking."""
import argparse
import os
from pathlib import Path
import subprocess
import tempfile


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--hdlcpp', required=True)
    parser.add_argument('--cxx', required=True)
    parser.add_argument('--work', required=True, type=Path)
    parser.add_argument('--cpphdl')
    parser.add_argument('--verilator')
    args = parser.parse_args()
    fixture = Path(__file__).resolve().parent
    include = fixture.parents[1] / 'include'
    args.work.mkdir(parents=True, exist_ok=True)
    root = Path(tempfile.mkdtemp(prefix='nested-packed-', dir=args.work))

    def run(command, label, env=None):
        result = subprocess.run(list(map(str, command)), cwd=work, env=env, text=True,
                                stdout=subprocess.PIPE, stderr=subprocess.STDOUT, timeout=180)
        (work / (label + '.log')).write_text(result.stdout)
        if result.returncode:
            raise RuntimeError(f'{form} {label}: {result.stdout[-6000:]}\nArtifacts: {work}')
        return result.stdout

    for form in ('TYPEDEF', 'HEADER', 'LOCAL', 'DEFAULT', 'ANONYMOUS', 'PACKAGE'):
        work = root / form.lower()
        work.mkdir()

        environment = dict(os.environ, HDLCPP_DEFINES='NESTED_' + form)
        run([args.hdlcpp, fixture / 'NestedPacked.sv'], 'convert', environment)
        seed = work / 'seed.cc'
        seed.write_text('#include "generated/NestedPacked.h"\n'
                        'extern NestedPacked<TEST_WIDTH> cpphdl_top;\n')
        for width in ((8,) if form == 'PACKAGE' else (8, 72)):
            label = str(width)
            output = work / label
            definitions = [f'-DTEST_WIDTH={width}']
            flags = ['-std=c++23', '-I' + str(include), '-I' + str(work), *definitions]
            runner = fixture / 'NestedPackedRun.cc'
            if args.cpphdl:
                run([args.cpphdl, '--native-graph', '--top', 'cpphdl_top', '--cxx', args.cxx,
                     '--frontend-flag=-I' + str(work), f'--frontend-flag=-DTEST_WIDTH={width}',
                     '--runner', runner, '--output', output, seed, '--', *flags,
                     '-DNESTED_GRAPH', '-fsanitize=address,undefined'], label + '-graph')
                print(form, run([output / 'run'], label + '-run'), end='')
            elif args.verilator:
                rtl_form = 'TYPEDEF' if form in ('HEADER', 'LOCAL', 'DEFAULT') else form
                run([args.verilator, '--cc', '--exe', '--build', '-j', '1', '-Wno-fatal',
                     '--top-module', 'NestedPacked', '--Mdir', output, f'-GWIDTH={width}',
                     '-DNESTED_' + rtl_form, '-CFLAGS', ' '.join(flags + ['-DNESTED_RTL']),
                     fixture / 'NestedPacked.sv', runner], label + '-rtl')
                print(form, 'reference=' + rtl_form,
                      run([output / 'VNestedPacked'], label + '-run'), end='')
            else:
                for optimization in ('-O0', '-O2'):
                    run([args.cxx, *flags, optimization, '-fsanitize=address,undefined',
                         runner, '-o', output], label + optimization + '-build')
                    print(form, optimization, run([output], label + optimization + '-run'), end='')

    form = 'EXTERNAL'
    work = root / 'external'
    work.mkdir()
    traits = work / 'traits.tsv'
    run([args.hdlcpp, fixture / 'NestedPackedExternal.sv'], 'package',
        dict(os.environ, HDLCPP_DEFINES='NESTED_PACKAGE', HDLCPP_WRITE_MODULE_TRAITS=str(traits)))
    (work / 'generated/NestedPackedExternal.h').rename(
        work / 'generated/NestedPackedExternalDeclaration.h')
    run([args.hdlcpp, fixture / 'NestedPackedExternal.sv'], 'consumer',
        dict(os.environ, HDLCPP_MODULE_TRAITS=str(traits)))
    assert 'type_field_lower.packet_t::__hdlcpp_inner_t.window.0=8' in traits.read_text()
    for optimization in ('-O0', '-O2'):
        output = work / 'run'
        run([args.cxx, '-std=c++23', '-I' + str(include), '-I' + str(work), optimization,
             '-fsanitize=address,undefined', fixture / 'NestedPackedExternalRun.cc', '-o', output],
            optimization + '-build')
        print(optimization, run([output], optimization + '-run'), end='')

    form = 'DEEP'
    work = root / 'deep'
    work.mkdir()
    run([args.hdlcpp, fixture / 'NestedPackedDeep.sv'], 'convert')
    seed = work / 'seed.cc'
    seed.write_text('#include "generated/NestedPackedDeep.h"\n'
                    'extern NestedPackedDeep<TEST_WIDTH> cpphdl_top;\n')
    for width in (4, 24):
        label = str(width)
        output = work / label
        flags = ['-std=c++23', '-I' + str(include), '-I' + str(work), f'-DTEST_WIDTH={width}']
        runner = fixture / 'NestedPackedDeepRun.cc'
        if args.cpphdl:
            run([args.cpphdl, '--native-graph', '--top', 'cpphdl_top', '--cxx', args.cxx,
                 '--frontend-flag=-I' + str(work), f'--frontend-flag=-DTEST_WIDTH={width}',
                 '--runner', runner, '--output', output, seed, '--', *flags,
                 '-DNESTED_GRAPH', '-fsanitize=address,undefined'], label + '-graph')
            print(run([output / 'run'], label + '-run'), end='')
        elif args.verilator:
            run([args.verilator, '--cc', '--exe', '--build', '-j', '1', '-Wno-fatal',
                 '--top-module', 'NestedPackedDeep', '--Mdir', output, f'-GWIDTH={width}',
                 '-CFLAGS', ' '.join(flags + ['-DNESTED_RTL']),
                 fixture / 'NestedPackedDeep.sv', runner], label + '-rtl')
            print(run([output / 'VNestedPackedDeep'], label + '-run'), end='')
        else:
            for optimization in ('-O0', '-O2'):
                run([args.cxx, *flags, optimization, '-fsanitize=address,undefined',
                     runner, '-o', output], label + optimization + '-build')
                print(optimization, run([output], label + optimization + '-run'), end='')


if __name__ == '__main__':
    main()
