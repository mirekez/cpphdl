"""Check nested clock selection against independent native and RTL counters."""
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
args.cxx = args.cxx.absolute()
source = Path(__file__).resolve().parent
repo = source.parents[1]
work = args.work.resolve()
work.mkdir(parents=True, exist_ok=True)
tools = TestToolchain(args.cxx, args.verilator.resolve(), work)
clocks = ['--primary_clock', 'fast_clk', '250000000', '--secondary_clock', 'slow_clk', '100000000']


def run(name, command):
    with (work / (name + '.log')).open('w') as log:
        subprocess.run(tools.command(command), env=tools.env, stdout=log, stderr=-2, check=True, timeout=120)


run('native-build', [args.cxx, '-std=c++17', '-I' + str(repo / 'include'),
                     source / 'ChildClockRun.cc', '-o', work / 'native'])
run('native', [work / 'native'])
run('convert', [args.cpphdl.resolve(), *clocks, '--generated-dir', work / 'rtl',
                source / 'ChildClock.cpp', '--', '-I' + str(repo / 'include')])
run('rtl-build', [args.verilator.resolve(), '--cc', '--exe', '--build', '-j', '1', '-Wno-fatal',
    '--top-module', 'ChildClock', '--Mdir', work / 'obj', '-CFLAGS', '-DVERILATOR -std=c++17',
    work / 'rtl/Predef_pkg.sv', work / 'rtl/ClockCounter.sv', work / 'rtl/ChildClock.sv',
    source / 'ChildClockRun.cc'])
run('rtl', [work / 'obj/VChildClock'])
print((work / 'native.log').read_text(), end='')
print((work / 'rtl.log').read_text(), end='')
result = subprocess.run([str(args.cpphdl.resolve()), *clocks, '--generated-dir', str(work / 'conflict'),
    str(source / 'ChildClock.cpp'), '--', '-I' + str(repo / 'include'), '-DCHILD_CLOCK_CONFLICT'],
    env=tools.env, capture_output=True, text=True, timeout=60)
if not result.returncode or 'conflicting child clock sources' not in result.stderr:
    raise ValueError('conflicting child clock sources were silently accepted: ' + result.stdout + result.stderr)
print('PASS conflicting clock sources for an indexed child are rejected')
