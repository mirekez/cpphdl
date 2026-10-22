"""Unused replacements need complete defaults; concrete instances keep actuals."""
import argparse
from pathlib import Path
import subprocess
import sys
import tempfile

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))
from TestToolchain import TestToolchain


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    for name in ('cpphdl', 'cxx', 'verilator', 'work'):
        parser.add_argument('--' + name, required=True)
    parser.add_argument('--kind', choices=('inline', 'file', 'script'), required=True)
    args = parser.parse_args()
    fixtures = Path(__file__).resolve().parent
    include = fixtures.parents[1] / 'include'
    base = Path(args.work) / args.kind
    base.mkdir(parents=True, exist_ok=True)
    with tempfile.TemporaryDirectory(dir=base) as temporary:
        work = Path(temporary)
        toolchain = TestToolchain(args.cxx, args.verilator, work)

        def run(command):
            result = subprocess.run(toolchain.command(command), cwd=work, env=toolchain.env,
                                    text=True, capture_output=True, timeout=90)
            if result.returncode:
                raise RuntimeError(result.stdout + result.stderr)
            return result.stdout

        for width in (8, 13):
            rtl = (f'`default_nettype none\nmodule Aaaaaa #(parameter int LANES = {width})\n'
                   '(input wire clk, input wire reset, input wire [LANES-1:0] value_in,\n'
                   ' output wire [LANES-1:0] value_out);\n'
                   'assign value_out = value_in;\nendmodule\n')
            (work / f'replacement_{width}.sv').write_text(rtl)
            script = work / f'replacement_{width}.sh'
            script.write_text('#!/bin/sh\ncat <<\'SV\'\n' + rtl + 'SV\n')
            script.chmod(0o755)

        kind = [] if args.kind == 'inline' else ['-DREPLACEMENT_' + args.kind.upper()]
        for case, defines, expected in (
                ('unused', [], None),
                ('default', ['-DREPLACEMENT_DEFAULT'], 8),
                ('used8', ['-DINSTANTIATE_AAAAAA', '-DREPLACEMENT_WIDTH=8'], 8),
                ('used13', ['-DINSTANTIATE_AAAAAA', '-DREPLACEMENT_WIDTH=13'], 13)):
            generated = work / case
            flags = ['-std=c++17', '-I' + str(include), *kind, *defines]
            output = run([args.cpphdl, '--generated-dir=' + str(generated),
                          fixtures / 'UnusedReplacement.h', '--', '-DSYNTHESIS', *flags])
            assert 'ERROR:' not in output, output
            replacement = generated / 'Aaaaaa.sv'
            assert replacement.exists() == (expected is not None), (
                f'{case}: replacement emitted without a value, or valid replacement missing\n'
                + (replacement.read_text() if replacement.exists() else output))
            if expected is not None:
                assert f'parameter int LANES = {expected}' in replacement.read_text()
            sources = [generated / 'Predef_pkg.sv', *sorted(
                path for path in generated.glob('*.sv') if path.name != 'Predef_pkg.sv')]
            for source in sources:
                assert '$(' not in source.read_text(), source.read_text()
            run([args.verilator, '--lint-only', '--top-module', 'Bbbbbb', *sources])
            native = work / 'native'
            run([args.cxx, *flags, fixtures / 'UnusedReplacementRun.cc', '-o', native])
            print(f'{args.kind}/{case}: ' + run([native]).strip(), flush=True)
            if case == 'used13':
                obj = work / 'obj'
                run([args.verilator, '--cc', '--exe', '--build', '-j', '1',
                     '--top-module', 'Bbbbbb', '--Mdir', obj,
                     '-CFLAGS', ' '.join([*flags, '-DREPLACEMENT_RTL']), *sources,
                     fixtures / 'UnusedReplacementRun.cc'])
                print(run([obj / 'VBbbbbb']), end='', flush=True)


if __name__ == '__main__':
    main()
