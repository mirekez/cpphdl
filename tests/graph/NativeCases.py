"""Check the supplied native-graph fixtures against their independent references."""
import argparse
import os
from pathlib import Path
import subprocess
import shutil

parser = argparse.ArgumentParser()
for name in ('cpphdl', 'cxx', 'work', 'case'):
    parser.add_argument('--' + name, required=True)
args = parser.parse_args()
root = Path(__file__).resolve().parents[2]
defines = {
    'NativeAssertions': 'CHECK_NATIVE_ASSERTIONS',
    'NativeControlLocals': 'CHECK_CONTROL_LOCALS',
    'NativeFloat': 'CHECK_NATIVE_FLOAT',
    'NativeMemoryGetters': 'CHECK_MEMORY_GETTERS',
    'NativeRecords': 'CHECK_NATIVE_RECORDS',
    'PortCopy': 'CHECK_PORT_COPY',
}
source = Path(__file__).with_name(args.case + '.cc')
work = Path(args.work).resolve()
work.mkdir(parents=True, exist_ok=True)
model = work / 'model'
if model.exists():
    shutil.rmtree(model)
env = dict(os.environ)
env['LD_LIBRARY_PATH'] = str(Path(args.cxx).resolve().parent.parent / 'lib') + ':' + env.get('LD_LIBRARY_PATH', '')

def run(command, label):
    with (work / (label + '.log')).open('w') as output:
        result = subprocess.run(list(map(str, command)), env=env, stdout=output,
                                stderr=subprocess.STDOUT, timeout=180)
    if result.returncode:
        raise RuntimeError((work / (label + '.log')).read_text()[-12000:])

run([args.cpphdl, '--native-graph', '--top', 'cpphdl_top', '--output', model,
     '--cxx', args.cxx, source], 'conversion')
run([args.cxx, '-std=c++23', '-O2', '-ffp-contract=off', '-D' + defines[args.case],
     '-I' + str(root / 'include'), '-I' + str(model), source, '-o', work / 'check'], 'build')
run([work / 'check'], 'simulation')
print((work / 'simulation.log').read_text())
