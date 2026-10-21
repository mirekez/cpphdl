"""Check alias ports before/after instantiation and SV parameter overrides."""
import argparse
from pathlib import Path
import subprocess
import sys
import tempfile

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))
from TestToolchain import TestToolchain


def main():
    parser = argparse.ArgumentParser()
    for name in ('cpphdl', 'cxx', 'verilator', 'work'):
        parser.add_argument('--' + name, required=True)
    args = parser.parse_args()
    fixtures = Path(__file__).resolve().parent
    root = fixtures.parents[1]
    base = Path(args.work)
    base.mkdir(parents=True, exist_ok=True)
    with tempfile.TemporaryDirectory(dir=base) as temporary:
        work = Path(temporary)
        tools = TestToolchain(args.cxx, args.verilator, work)

        def run(command):
            result = subprocess.run(tools.command(command), cwd=work, env=tools.env,
                                    capture_output=True, text=True, timeout=90)
            if result.returncode:
                raise RuntimeError(result.stdout + result.stderr)
            return result.stdout

        for instantiated in (False, True):
            generated = work / ('concrete' if instantiated else 'dependent')
            run([args.cpphdl, '--generated-dir=' + str(generated), fixtures / 'PortAliases.h',
                 '--', '-I' + str(root / 'include'),
                 *(['-DPORT_ALIAS_INSTANTIATE'] if instantiated else [])])
            rtl = (generated / 'PortAliases.sv').read_text()
            assert 'cpphdl_port' not in rtl and 'Binding' not in rtl, rtl
            for width in (5, 33):
                flags = ['-std=c++17', '-I' + str(root / 'include'), f'-DPORT_ALIAS_WIDTH={width}']
                native = work / 'native'
                run([args.cxx, *flags, fixtures / 'PortAliasesRun.cc', '-o', native])
                print(run([native]), end='')
                obj = work / f'obj-{instantiated}-{width}'
                run([args.verilator, '--cc', '--exe', '--build', '-j', '1', '-Wno-fatal',
                     '--top-module', 'PortAliases', f'-GWIDTH={width}', '--Mdir', obj,
                     '-CFLAGS', ' '.join([*flags, '-DPORT_ALIAS_RTL']),
                     generated / 'Predef_pkg.sv', generated / 'PortAliases.sv',
                     fixtures / 'PortAliasesRun.cc'])
                print(run([obj / 'VPortAliases']), end='')


if __name__ == '__main__':
    main()
