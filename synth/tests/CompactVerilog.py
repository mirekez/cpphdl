import argparse
import os
from pathlib import Path
import subprocess

parser = argparse.ArgumentParser()
for name in ('generator', 'verilator', 'cxx', 'work'):
    parser.add_argument('--' + name, required=True)
args = parser.parse_args()
work = Path(args.work).resolve()
work.mkdir(parents=True, exist_ok=True)
env = dict(os.environ)
lib = Path(args.cxx).resolve().parent.parent / 'lib'
env['LD_LIBRARY_PATH'] = str(lib) + ':' + env.get('LD_LIBRARY_PATH', '')
def run(command):
    result = subprocess.run(list(map(str, command)), env=env, text=True,
                            stdout=subprocess.PIPE, stderr=subprocess.STDOUT, timeout=90)
    if result.returncode:
        raise RuntimeError(result.stdout)
    print(result.stdout[-1000:])
run([args.generator, work / 'gates.v'])
run([args.verilator, '--cc', '--exe', '--build', '-j', '2', '-Wno-fatal',
     '--top-module', 'CompactMux', '--Mdir', work / 'obj', work / 'gates.v',
     Path(__file__).with_suffix('.cpp'), '-CFLAGS', '-std=c++17 -DVERILATOR',
     '-MAKEFLAGS', f'CXX={args.cxx} LINK={args.cxx} LDFLAGS=-L{lib}'])
run([work / 'obj/VCompactMux'])
