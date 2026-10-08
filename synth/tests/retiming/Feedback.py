import argparse
import json
import os
import shutil
from pathlib import Path
import subprocess

p = argparse.ArgumentParser()
for arg in ('cpphdl', 'cxx', 'verilator', 'work', 'case'):
    p.add_argument('--' + arg, required=True)
a = p.parse_args()
root = Path(__file__).resolve().parents[3]
source = Path(__file__).with_name(a.case + '.cpp')
work = Path(a.work).resolve()
work.mkdir(parents=True, exist_ok=True)
for generated in ('rtl', 'too-long', 'effect', 'obj'):
    if (work / generated).exists():
        shutil.rmtree(work / generated)
lib = Path(a.cxx).resolve().parent.parent / 'lib'
env = dict(os.environ, LD_LIBRARY_PATH=str(lib) + ':' + os.environ.get('LD_LIBRARY_PATH', ''))

def run(command, label, failure=None):
    result = subprocess.run(list(map(str, command)), cwd=work, env=env, text=True,
                            stdout=subprocess.PIPE, stderr=subprocess.STDOUT, timeout=300)
    if result.returncode and '--output' in command:
        directory = Path(command[command.index('--output') + 1])
        for log in sorted(directory.glob('*.log')):
            result.stdout += '\n' + log.name + '\n' + log.read_text()[-4000:]
    (work / (label + '.log')).write_text(result.stdout)
    if failure is not None:
        if result.returncode == 0 or failure not in result.stdout:
            raise RuntimeError(label + ': expected rejection: ' + failure + '\n' + result.stdout[-5000:])
    elif result.returncode:
        raise RuntimeError(label + '\n' + result.stdout[-6000:])
    return result.stdout

command = [a.cpphdl, '--synth', '--top', 'cpphdl_top', '--module', 'SynthRetiming',
           '--cxx', a.cxx, '--retiming', 'fit_pipeline_retiming',
           '--clock-period-ns', '3.205128205' if a.case == 'feedback_frames' else '2.5']
run([*command, '--output', work / 'rtl', source], 'synth')
report = json.loads((work / 'rtl/timing.json').read_text())
rule = report['rules'][0]
assert rule['target_met'] and rule['added_latency'] > 0, rule
if a.case == 'feedback_frames':
    assert rule['feedback_scheduled'] and rule['initiation_interval'] == rule['added_latency'] + 1, rule
else:
    assert len(report['keep_boxes']) == 2, report
    body = (work / 'rtl/keep_boxes.v').read_text()
    assert 'always' not in body, 'one-clock function acquired internal registers'
    cells = json.loads((work / 'rtl/gates.json').read_text())['modules']['SynthRetiming']['cells']
    names = {box['module'] for box in report['keep_boxes']}
    assert sum(cell['type'] in names for cell in cells.values()) == 2
    run([*command, '--output', work / 'too-long', source, '--', '-DTOO_LONG'],
        'too-long', 'one-clock function exceeds target period')
    run([*command, '--output', work / 'effect', source, '--', '-DBAD_EFFECT'],
        'effect', 'CPPHDL_ONE_CLOCK requires a pure combinational function')
definitions = ['-DRETIMING_RUN', '-DRETIMING_LATENCY=' + str(rule['added_latency'])]
run([a.cxx, '-std=c++17', '-O1', '-I' + str(root / 'include'), work / 'rtl/retimed_graph.cc',
     '-o', work / 'emit-native'], 'native-compiler')
run([work / 'emit-native', work / 'model.h'], 'native-emit')
run([a.cxx, '-std=c++17', '-O1', '-I' + str(root / 'include'), '-I' + str(work),
     *definitions, '-DRETIMING_GRAPH', source, '-o', work / 'native'], 'native-build')
print(run([work / 'native'], 'native-test'))
run([a.verilator, '--cc', '--exe', '--build', '-j', '2', '--top-module', 'SynthRetiming', '-Wno-fatal',
     '--Mdir', work / 'obj', work / 'rtl/gates.v', source, '-CFLAGS',
     ' '.join(['-std=c++17', '-I' + str(root / 'include'), *definitions]),
     '-MAKEFLAGS', f'CXX={a.cxx} LINK={a.cxx} AR=ar LDFLAGS=-L{lib}'], 'verilator-build')
print(run([work / 'obj/VSynthRetiming'], 'gate-test'))
print(rule)
