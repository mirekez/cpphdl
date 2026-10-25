"""Namespaced RTL methods must not be mistaken for constructors or primitives."""
import argparse
from pathlib import Path
import subprocess
import sys

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))
from TestToolchain import TestToolchain

parser = argparse.ArgumentParser(description=__doc__)
for name in ('cpphdl', 'cxx', 'verilator', 'work'):
    parser.add_argument('--' + name, type=Path, required=True)
args = parser.parse_args()
source = Path(__file__).resolve().parent
repo = source.parents[1]
work = args.work.resolve()
work.mkdir(parents=True, exist_ok=True)
tools = TestToolchain(args.cxx.absolute(), args.verilator.resolve(), work)

def run(name, command):
    result = subprocess.run(tools.command(command), env=tools.env,
                            text=True, capture_output=True, timeout=120)
    (work / (name + '.log')).write_text(result.stdout + result.stderr)
    if result.returncode:
        raise RuntimeError(name + ':\n' + result.stdout + result.stderr)

runner = source / 'NamespacedMethodsRun.cc'
run('native-build', [args.cxx, '-std=c++17', '-I' + str(repo / 'include'), runner, '-o', work / 'native'])
run('native', [work / 'native'])
rtl = work / 'rtl'
for path in rtl.glob('*.sv'):
    path.unlink()
run('convert', [args.cpphdl.resolve(), '--generated-dir', rtl, source / 'NamespacedMethods.h',
                '--', '-I' + str(repo / 'include')])
files = sorted(rtl.glob('*.sv'), key=lambda path: (path.name != 'Predef_pkg.sv', not path.name.endswith('_pkg.sv'), path.name))
run('rtl-build', [args.verilator.resolve(), '--cc', '--exe', '--build', '-j', '1', '-Wno-fatal',
    '--top-module', 'NamespacedMethods', '--Mdir', work / 'obj', '-CFLAGS', '-DVERILATOR -std=c++17',
    *files, runner])
run('rtl', [work / 'obj/VNamespacedMethods'])
print((work / 'native.log').read_text() + (work / 'rtl.log').read_text(), end='')
