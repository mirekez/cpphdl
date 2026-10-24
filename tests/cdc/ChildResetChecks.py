"""Exercise child reset expressions against independent native/RTL oracles."""
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
tools = TestToolchain(args.cxx, args.verilator.resolve(), work)

def run(name, command):
    with (work / (name + '.log')).open('w') as log:
        subprocess.run(tools.command(command), env=tools.env, stdout=log, stderr=subprocess.STDOUT,
                       check=True, timeout=120)

run('native-build', [args.cxx, '-std=c++17', '-I' + str(repo / 'include'),
                    source / 'ChildResetRun.cc', '-o', work / 'native'])
run('native', [work / 'native'])
run('convert', [args.cpphdl.resolve(), '--generated-dir', work / 'rtl', source / 'ChildReset.cpp',
                '--', '-I' + str(repo / 'include')])
run('rtl-build', [args.verilator.resolve(), '--cc', '--exe', '--build', '-j', '1', '-Wno-fatal',
    '--top-module', 'ChildReset', '--Mdir', work / 'obj', '-CFLAGS', '-DVERILATOR -std=c++17',
    work / 'rtl/Predef_pkg.sv', work / 'rtl/ResetCounter.sv', work / 'rtl/ChildReset.sv',
    source / 'ChildResetRun.cc'])
run('rtl', [work / 'obj/VChildReset'])
print((work / 'native.log').read_text(), end='')
print((work / 'rtl.log').read_text(), end='')
for define, message in [('CHILD_RESET_LOCAL', 'process-local variable'),
                        ('CHILD_RESET_CONFLICT', 'conflicting child reset arguments')]:
    result = subprocess.run([str(args.cpphdl.resolve()), '--generated-dir', str(work / define),
        str(source / 'ChildReset.cpp'), '--', '-I' + str(repo / 'include'), '-D' + define],
        capture_output=True, text=True, timeout=60, env=tools.env)
    if not result.returncode or message not in result.stderr:
        raise ValueError('unsupported child reset was silently accepted: ' + define + result.stdout + result.stderr)
print('PASS ambiguous/process-local child reset expressions are rejected')
