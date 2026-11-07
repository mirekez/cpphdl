"""Exercise the tuple CPU's unsigned divide/remainder in native C++ and RTL."""
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
                            stdout=subprocess.PIPE, stderr=subprocess.STDOUT,
                            text=True, errors='replace', timeout=180)
    (work / (name + '.log')).write_text(result.stdout)
    if result.returncode:
        raise RuntimeError(f'{name} failed ({result.returncode}):\n{result.stdout}')
    return result.stdout


run('native-build', [args.cxx.absolute(), '-std=c++23', '-O2', '-I' + str(repo / 'include'),
                     source / 'ExecuteArithmeticRun.cc', '-o', work / 'native'])
native = run('native', [work / 'native'])
run('convert', [args.cpphdl.resolve(), '--generated-dir', work / 'rtl',
                source / 'ExecuteArithmetic.h', '--', '-I' + str(repo / 'include')])
rtl = work / 'rtl'
files = [rtl / 'Predef_pkg.sv']
files += sorted(p for p in rtl.glob('*_pkg.sv') if p not in files)
files += [rtl / 'ExecuteArithmetic.sv']
run('rtl-build', [args.verilator.resolve(), '--cc', '--exe', '--build', '-j', '2',
    '-Wno-fatal', '--top-module', 'ExecuteArithmetic', '-y', rtl, '--Mdir', work / 'obj',
    '-CFLAGS', '-DVERILATOR -std=c++23', *files, source / 'ExecuteArithmeticRun.cc'])
result = run('rtl', [work / 'obj/VExecuteArithmetic'])
if native != result:
    raise RuntimeError(f'Native/RTL results differ:\n{native}\n{result}')
print(result, end='')
