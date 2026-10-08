"""Check static owned hierarchy and reject constructor side effects/aliasing."""
import argparse
import os
from pathlib import Path
import subprocess
import tempfile

parser = argparse.ArgumentParser()
for name in ('cpphdl', 'cxx', 'work'):
    parser.add_argument('--' + name, required=True)
args = parser.parse_args()
root = Path(__file__).resolve().parents[2]
Path(args.work).mkdir(parents=True, exist_ok=True)
work = Path(tempfile.mkdtemp(prefix='owned-', dir=args.work))
env = dict(os.environ)
env['LD_LIBRARY_PATH'] = str(Path(args.cxx).resolve().parent.parent / 'lib') + ':' + env.get('LD_LIBRARY_PATH', '')
source = Path(__file__).with_name('OwnedHierarchy.cc')
def run(command, ok=True):
    result = subprocess.run(list(map(str, command)), text=True, stdout=subprocess.PIPE,
                            stderr=subprocess.STDOUT, env=env, timeout=120)
    if ok and result.returncode:
        raise RuntimeError(result.stdout)
    return result
for filename, define in [('OwnedHierarchy.cc', 'CHECK_OWNED_HIERARCHY'),
                         ('FirtoolBits.cc', 'CHECK_FIRTOOL_BITS'),
                         ('FirtoolMemo.cc', 'CHECK_FIRTOOL_MEMO'),
                         ('FirtoolCompare.cc', 'CHECK_FIRTOOL_COMPARE')]:
    fixture = Path(__file__).with_name(filename)
    output = work / fixture.stem
    run([args.cpphdl, '--native-graph', '--top', 'cpphdl_top', '--output', output,
         '--cxx', args.cxx, fixture])
    run([args.cxx, '-std=c++23', '-O2', '-D' + define, '-I' + str(root / 'include'),
         '-I' + str(output), fixture, '-o', output / 'check'])
    print(run([output / 'check']).stdout)
original = source.read_text()
for name, body in {
    'alias': 'left = new OwnedLeaf(); right = left;',
    'duplicate': 'left = new OwnedLeaf(); left = new OwnedLeaf(); right = new OwnedLeaf();',
    'side_effect': 'left = new OwnedLeaf(); right = new OwnedLeaf(); left->saved._next = 7;',
    'uninitialized': 'left = new OwnedLeaf();',
}.items():
    fixture = work / (name + '.cc')
    fixture.write_text(original.replace('left = new OwnedLeaf(); right = new OwnedLeaf();', body))
    result = run([args.cpphdl, '--lower-cpp-graph', fixture, work / (name + '.graph.cc'),
                  'cpphdl_top', '--', '-std=c++23', '-I' + str(root / 'include')], ok=False)
    if not result.returncode or ('constructor unsupported' not in result.stdout and
                                 'not initialized by a unique' not in result.stdout):
        raise AssertionError(name + ': ' + result.stdout)
print('constructor rejection checks PASS')

fixture = work / 'bad-padding.cc'
fixture.write_text(Path(__file__).with_name('FirtoolBits.cc').read_text().replace('result.set(bit, 0)', 'result.set(bit, 1)'))
result = run([args.cpphdl, '--lower-cpp-graph', fixture, work / 'bad-padding.graph.cc',
              'cpphdl_top', '--', '-std=c++23', '-I' + str(root / 'include')], ok=False)
if not result.returncode or 'nonzero or out-of-storage logic padding write' not in result.stdout:
    raise AssertionError(result.stdout)
print('nonzero padding rejection PASS')

fixture = work / 'bad-memo.cc'
fixture.write_text(Path(__file__).with_name('FirtoolMemo.cc').read_text().replace('return calculate_cache;', 'return calculate_cache = 42;'))
result = run([args.cpphdl, '--lower-cpp-graph', fixture, work / 'bad-memo.graph.cc',
              'cpphdl_top', '--', '-std=c++23', '-I' + str(root / 'include')], ok=False)
if not result.returncode or 'unbound C++ declaration: _system_clock' not in result.stdout:
    raise AssertionError('effectful clock guard was discarded: ' + result.stdout)
print('modified memoization guard rejection PASS')
