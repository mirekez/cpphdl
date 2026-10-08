import argparse
import json
import os
from pathlib import Path
import shutil
import subprocess

parser = argparse.ArgumentParser()
for name in ('cpphdl', 'cxx', 'verilator', 'work'):
    parser.add_argument('--' + name, required=True)
parser.add_argument('--ram', action='store_true')
parser.add_argument('--hft', action='store_true')
args = parser.parse_args()
root = Path(__file__).resolve().parents[3]
source = root / 'hls/examples/net/hft.cpp' if args.hft else Path(__file__).with_name('ClockedSchedule.cpp')
top = 'Hft' if args.hft else 'ScheduledTop'
tool_timeout = 900 if args.hft else 300
work = Path(args.work).resolve()
if work.exists():
    shutil.rmtree(work)
work.mkdir(parents=True)
flags = [] if args.hft else ['-DHLS_RAM=' + str(int(args.ram))]
atomic = subprocess.check_output([args.cxx, '-print-file-name=libatomic.so'], text=True).strip()
lib = Path(atomic).parent
runtime = Path(subprocess.check_output([args.cxx, '-print-file-name=libstdc++.so'], text=True).strip()).resolve().parent
env = dict(os.environ, LD_LIBRARY_PATH=str(runtime) + ':' + os.environ.get('LD_LIBRARY_PATH', ''))


def run(command, label):
    with (work / (label + '.log')).open('w') as log:
        subprocess.run(list(map(str, command)), env=env, stdout=log, stderr=subprocess.STDOUT, check=True,
                       timeout=1800 if label.endswith('-synth') and args.hft else tool_timeout)


run([args.cxx, '-std=c++17', '-O1', '-I' + str(root / 'include'), *flags, source, '-o', work / 'native'], 'native-build')
run([work / 'native'], 'native')
run([args.cpphdl, '--generated-dir', work / 'ordinary', source, '--', *flags], 'ordinary')
assert not list((work / 'ordinary').glob('*.cc'))
assert not list((work / 'ordinary').glob('*.json'))
if not args.hft:
    rtl = work / 'ordinary'
    sources = [rtl / 'Predef_pkg.sv', *sorted(rtl.glob('cpphdl_hls_*.sv')), rtl / (top + '.sv')]
    run([args.verilator, '--cc', '--exe', '--build', '-j', '2', '-Wno-fatal',
         '-MAKEFLAGS', f'CXX={args.cxx} LINK={args.cxx} AR=ar LDFLAGS=-L{lib}',
         '--top-module', top, '--Mdir', work / 'ordinary-obj', *sources, source,
         '-CFLAGS', '-std=c++17 -DVERILATOR -I' + str(root / 'include') + ' ' + ' '.join(flags)], 'ordinary-verilator')
    run([work / 'ordinary-obj' / ('V' + top)], 'ordinary-simulation')
    for case, message in ((1, 'work must be unconditional'), (2, 'missing scheduled Clocked work/strobe'),
                          (3, 'missing scheduled Clocked work/strobe')):
        target = work / f'rejected-{case}.cc'
        command = [args.cpphdl, '--lower-synthesis-graph', str(source), str(target), top,
                   '--', '-std=c++17', '-I' + str(root / 'include'), '-DSYNTHESIS', f'-DSYNTH_SCHEDULE_ERROR={case}']
        result = subprocess.run(command, env=env, text=True, capture_output=True, timeout=60)
        assert result.returncode and message in result.stderr and not target.exists(), result.stderr
for mode in ('baseline', 'retimed'):
    out = work / mode
    period = '3.174603175' if args.hft else '3.205128205'
    retiming = ['--retiming', 'fit_pipeline_retiming', '--clock-period-ns', period] if mode == 'retimed' else []
    run([args.cpphdl, '--synth', '--top', top, '--module', top,
         '--output', out, '--cxx', args.cxx, '--tool-timeout', str(tool_timeout),
         *retiming, source, '--', *flags], mode + '-synth')
    report = json.loads((out / 'manifest.json').read_text())
    assert report['status'] == 'complete'
    assert report['graph_frontend'] == 'C++ RTL with automatic AST Clocked scheduling -> CppHDL graph'
    assert not (out / 'scheduled').exists(), 'graph export must not round-trip through SV'
    assert not any('yosys' in str(arg).lower() for cmd in report['commands'] for arg in cmd)
    if mode == 'retimed':
        rule = report['timing']['rules'][0]
        assert rule['target_met'] and rule['added_latency'] > 0
        if args.hft:
            assert not rule['feedback_scheduled'] and rule['initiation_interval'] == 1
        else:
            assert rule['feedback_scheduled']
    drain = ''
    if args.hft:
        regions = report['timing']['streaming_regions']
        assert len(regions) == 3 and all(p['initiation_interval'] == 1 for p in regions)
        drain = ' -DHFT_TEST_DRAIN_CYCLES=' + str(sum(p['latency'] for p in regions) + 7 + 64)
    run([args.verilator, '--cc', '--exe', '--build', '-j', '2', '-Wno-fatal',
         '-MAKEFLAGS', f'CXX={args.cxx} LINK={args.cxx} AR=ar LDFLAGS=-L{lib}',
         '--top-module', top, '--Mdir', out / 'obj', out / 'gates.v', source,
         '-CFLAGS', '-std=c++17 -O3 -DVERILATOR -I' + str(root / 'include') + ' ' + ' '.join(flags) +
         drain + (' -DRETIMED' if mode == 'retimed' and not args.hft else '')], mode + '-verilator')
    assert 'Warning-UNOPTFLAT' not in (work / (mode + '-verilator.log')).read_text()
    run([out / ('obj/V' + top)], mode + '-simulation')
    if mode == 'baseline' and not args.hft:
        assert (work / 'ordinary-simulation.log').read_text() == (work / 'baseline-simulation.log').read_text(), \
            'scheduled graph changed RTL cycle timing or output trace'
print('Full HFT native and scheduled graph gate-level checks passed' if args.hft else
      'Clocked native, scheduled graph and retimed gate-level checks passed')
