"""Check module-local clocks without adding them to the root clock interface."""
import argparse
import json
from pathlib import Path
import subprocess
import sys

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))
from TestToolchain import TestToolchain

parser = argparse.ArgumentParser(description=__doc__)
for name in ('cpphdl', 'cxx', 'verilator', 'work'):
    parser.add_argument('--' + name, type=Path, required=True)
parser.add_argument('--ctest', type=Path)
parser.add_argument('--build', type=Path)
args = parser.parse_args()
if bool(args.ctest) != bool(args.build):
    parser.error('--ctest and --build must be provided together')
if args.build:
    listing = subprocess.check_output([str(args.ctest), '--test-dir', str(args.build),
                                       '--show-only=json-v1'], text=True, timeout=60)
    names = [test['name'] for test in json.loads(listing)['tests']]
    for fixture in ('LocalClocks', 'ChildClock', 'ChildReset'):
        if 'cdc_' + fixture in names:
            raise ValueError(f'{fixture}.cpp is model-only, not a standalone executable')
    if names.count('child_local_clocks') != 1:
        raise ValueError('Expected exactly one child_local_clocks regression')
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
