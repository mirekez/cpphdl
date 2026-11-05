"""Check module-local clocks without adding them to the root clock interface."""
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
clocks = ['--primary_clock', 'fast_clk', '250000000', '--secondary_clock', 'slow_clk', '100000000',
          '--secondary_clock', 'spare_clk', '50000000']


def run(name, command):
    with (work / (name + '.log')).open('w') as log:
        subprocess.run(tools.command(command), env=tools.env, stdout=log, stderr=-2, check=True, timeout=180)


run('native-build', [args.cxx.absolute(), '-std=c++17', '-I' + str(repo / 'include'),
                     source / 'LocalClocksRun.cc', '-o', work / 'native'])
run('native', [work / 'native'])
run('convert', [args.cpphdl.resolve(), *clocks, '--generated-dir', work / 'rtl',
                source / 'LocalClocks.cpp', '--', '-I' + str(repo / 'include')])
if 'MISSED CALL' in (work / 'convert.log').read_text():
    raise ValueError('local clock lifecycle calls were not recognized')
top = (work / 'rtl/LocalClocks.sv').read_text()
if 'input wire source_clk' in top or 'input wire destination_clk' in top:
    raise ValueError('child clock names leaked into root ports')
run('rtl-build', [args.verilator.resolve(), '--cc', '--exe', '--build', '-j', '1', '-Wno-fatal',
    '--top-module', 'LocalClocks', '--Mdir', work / 'obj', '-CFLAGS', '-DVERILATOR -std=c++17',
    work / 'rtl/Predef_pkg.sv', work / 'rtl/LocalClockPair.sv', work / 'rtl/LocalClocks.sv',
    source / 'LocalClocksRun.cc'])
run('rtl', [work / 'obj/VLocalClocks'])
if (work / 'native.log').read_text() != (work / 'rtl.log').read_text():
    raise ValueError('native/RTL local clock results differ')
result = subprocess.run([str(args.cpphdl.resolve()), *clocks, '--generated-dir', str(work / 'missing'),
    str(source / 'LocalClocks.cpp'), '--', '-I' + str(repo / 'include'), '-DLOCAL_CLOCK_MISSING_STROBE'],
    env=tools.env, capture_output=True, text=True, timeout=60)
if not result.returncode or '_strobe_destination_clk' not in result.stderr:
    raise ValueError('incomplete local process was accepted')
print((work / 'rtl.log').read_text(), end='')
print('PASS incomplete local clock process rejected')
