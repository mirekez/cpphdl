import argparse
import os
from pathlib import Path
import shutil
import subprocess

parser = argparse.ArgumentParser()
for name in ('cpphdl', 'cxx', 'work'):
    parser.add_argument('--' + name, required=True)
args = parser.parse_args()
work = Path(args.work).resolve()
work.mkdir(parents=True, exist_ok=True)
destination = work / 'signed-arithmetic'
if destination.exists():
    shutil.rmtree(destination)
source = Path(__file__).with_name('SignedArithmetic.cc')
include = source.parents[2] / 'include'
env = os.environ.copy()
lib = Path(args.cxx).resolve().parent.parent / 'lib'
env['LD_LIBRARY_PATH'] = str(lib) + ':' + env.get('LD_LIBRARY_PATH', '')
command = [args.cpphdl, '--native-graph', '--top', 'cpphdl_top', '--cxx', args.cxx,
           '--frontend-flag=-std=c++17', '--output', str(destination), '--runner', str(source),
           str(source), '--', '-std=c++17', '-DSIGNED_ARITHMETIC_RUN', '-I' + str(include)]
try:
    subprocess.run(command, check=True, env=env, timeout=180)
    subprocess.run([str(destination / 'run')], check=True, env=env, timeout=30)
except subprocess.SubprocessError:
    for log in destination.glob('*.log'):
        print(str(log) + '\n' + log.read_text()[-6000:])
    raise
