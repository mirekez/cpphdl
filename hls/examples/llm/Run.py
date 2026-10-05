"""Check DDR/Q48 compute in ordinary RTL and retimed, mapped gate-level RTL."""
import argparse
import json
import os
from pathlib import Path
import shutil
import subprocess

p = argparse.ArgumentParser()
for name in ('cpphdl', 'cxx', 'verilator', 'work'):
    p.add_argument('--' + name, required=True)
p.add_argument('--bus', type=int, choices=(64, 128, 256, 512), default=512)
p.add_argument('--gates', action='store_true')
p.add_argument('--top', choices=('WeightProduct', 'ScalarMath', 'MatrixMath', 'TiledMatVec'), default='WeightProduct')
args = p.parse_args()
root = Path(__file__).resolve().parents[3]
source = Path(__file__).with_name(args.top + '.cpp')
work = Path(args.work).resolve()
work.mkdir(parents=True, exist_ok=True)
atomic = Path(subprocess.check_output([args.cxx, '-print-file-name=libatomic.so'], text=True).strip()).parent
runtime = Path(subprocess.check_output([args.cxx, '-print-file-name=libstdc++.so'], text=True).strip()).resolve().parent
env = dict(os.environ, LD_LIBRARY_PATH=str(runtime) + ':' + os.environ.get('LD_LIBRARY_PATH', ''))
flags = ['-DLLM_DDR_BITS=' + str(args.bus), '-I' + str(root / 'include')]


def run(command, label, timeout=600):
    with (work / (label + '.log')).open('w') as log:
        result = subprocess.run(list(map(str, command)), env=env, stdout=log, stderr=subprocess.STDOUT, timeout=timeout)
    if result.returncode:
        raise RuntimeError(f'{label} failed; see {work / (label + ".log")}')


run([args.cxx, '-std=c++17', '-O1', *flags, source, '-o', work / 'native'], 'native-build')
run([work / 'native'], 'native-test')
rtl = work / 'rtl'
if rtl.exists():
    shutil.rmtree(rtl)
if args.gates:
    run([args.cpphdl, '--synth', '--top', args.top, '--module', args.top,
         '--retiming', 'fit_pipeline_retiming', '--clock-period-ns', '5', '--output', rtl,
         '--cxx', args.cxx, source, '--', *flags], 'synthesis', 1200)
    report = json.loads((rtl / 'manifest.json').read_text())
    assert report['status'] == 'complete'
    timing = report['timing']
    assert timing['rules'][0]['target_met']
    assert timing['streaming_regions']
    assert all(region['initiation_interval'] == 1 for region in timing['streaming_regions'])
    if args.top == 'ScalarMath':
        boxes = timing['external_blackboxes']
        assert {box['module'] for box in boxes} == {'llm_q48_divide', 'llm_q48_exp_negative', 'llm_q48_inverse_sqrt', 'llm_q48_silu'}
        assert all(box['delay_ns'] == 0 and box['latency_cycles'] == 0 for box in boxes)
        assert (rtl / 'blackboxes.v').exists()
    sources = [rtl / 'gates.v']
else:
    run([args.cpphdl, '--generated-dir', rtl, source, '--', *flags], 'conversion')
    sources = [rtl / 'Predef_pkg.sv', *sorted(rtl.glob('cpphdl_hls_*.sv'))]
    sources += [path for path in sorted(rtl.glob('*.sv')) if path not in sources]
if args.top == 'ScalarMath':
    sources.append(source.with_name('MathModels.sv'))
obj = work / 'obj'
if obj.exists():
    shutil.rmtree(obj)
run([args.verilator, '--cc', '--exe', '--build', '-j', '2', '-Wno-fatal',
     '--top-module', args.top, '--Mdir', obj,
     '-MAKEFLAGS', f'CXX={args.cxx} LINK={args.cxx} AR=ar LDFLAGS=-L{atomic}',
     '-CFLAGS', '-std=c++17 -O1 -DVERILATOR ' + ' '.join(flags),
     *sources, source], 'verilator-build', 1200)
build_log = (work / 'verilator-build.log').read_text()
assert not any('Warning-' + warning in build_log for warning in ('LATCH', 'MULTIDRIVEN', 'UNOPTFLAT'))
run([obj / ('V' + args.top)], 'simulation', 600)
print((work / 'simulation.log').read_text())
