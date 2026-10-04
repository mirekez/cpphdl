import argparse
import os
from pathlib import Path
import subprocess

parser = argparse.ArgumentParser()
for name in ('cpphdl', 'cxx', 'work'):
    parser.add_argument('--' + name, required=True)
args = parser.parse_args()
root = Path(__file__).resolve().parents[2]
work = Path(args.work).resolve()
work.mkdir(parents=True, exist_ok=True)
if (work / 'graph.cc').exists():
    (work / 'graph.cc').unlink()
env = os.environ.copy()
lib = Path(args.cxx).resolve().parent.parent / 'lib'
env['LD_LIBRARY_PATH'] = str(lib) + ':' + env.get('LD_LIBRARY_PATH', '')
def run(command):
    subprocess.run(list(map(str, command)), check=True, env=env, timeout=180)
run([args.cpphdl, '--lower-cpp-graph', root / 'synth/tests/retiming/keep_box.cpp',
     work / 'graph.cc', 'cpphdl_top', '--', '-std=c++17', '-I' + str(root / 'include')])
run([args.cxx, '-std=c++17', '-O1', '-DCPPHDL_GRAPH_NO_MAIN', '-DCPPHDL_GRAPH_CORE_ONLY',
     '-I' + str(root / 'include'), work / 'graph.cc', Path(__file__).with_name('ClosureScope.cc'), '-o', work / 'check'])
run([work / 'check'])
