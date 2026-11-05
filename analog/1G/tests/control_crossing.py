"""Compare coherent control updates through unrelated and stopped clocks."""
import argparse
import json
import os
from pathlib import Path
import re
from hardware import configure_cleanup, execute, source_dependencies

parser = argparse.ArgumentParser(description=__doc__)
for name in ('converter', 'verilator', 'cxx', 'work'):
    parser.add_argument('--' + name, type=Path, required=True)
args = parser.parse_args()
configure_cleanup()
source = Path(__file__).resolve().parent
repo = source.parents[2]
work = args.work.resolve()
work.mkdir(parents=True, exist_ok=True)
entry = source / 'control_crossing_entry.cpp'
fixture = source / 'control_crossing.cpp'
report = dict(status='running', scope='coherent control crossing, not analog timing qualification')


def command(label, argv):
    with (work / (label + '.log')).open('w') as log:
        execute(list(map(str, argv)), cwd=work, env=os.environ, stdout=log, stderr=-2,
                check=True, timeout=240, memory_exclusive=False)


try:
    report['source_sha256'] = source_dependencies([entry, fixture], repo, str(args.cxx))
    command('native-build', [args.cxx.absolute(), '-std=c++17', '-O1', '-I' + str(repo / 'include'),
                            fixture, '-o', work / 'native'])
    command('native', [work / 'native'])
    command('convert', [args.converter.resolve(), '--primary_clock', 'source_clk', '250000000',
                       '--secondary_clock', 'destination_clk', '125000000',
                       '--generated-dir', work / 'rtl', entry, '--', '-I' + str(repo / 'include')])
    command('rtl-build', [args.verilator.resolve(), '--cc', '--exe', '--build', '-j', '1', '-Wno-fatal',
                         '--top-module', 'ControlCrossing', '-GWidth=32', '--Mdir', work / 'obj',
                         '-CFLAGS', '-DVERILATOR -std=c++17', '-MAKEFLAGS', 'CXX=' + str(args.cxx.absolute()) +
                         ' LINK=' + str(args.cxx.absolute()), work / 'rtl/Predef_pkg.sv',
                         work / 'rtl/ControlCrossing.sv', fixture])
    command('rtl', [work / 'obj/VControlCrossing'])
    native = (work / 'native.log').read_text().strip()
    rtl = (work / 'rtl.log').read_text().strip()
    if native != rtl or not re.fullmatch(r'PASS coherent control crossing: \d+ updates; \d+ digest', rtl):
        raise ValueError('control crossing independent checks or cycle comparison failed')
    report.update(status='pass', result=rtl)
    print(rtl)
except BaseException as error:
    report.update(status='fail', error=str(error))
    raise
finally:
    (work / 'checks.json').write_text(json.dumps(report, indent=2) + '\n')
