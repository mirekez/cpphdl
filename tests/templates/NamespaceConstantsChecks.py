"""Validate namespace constants without losing symbolic module constants."""
import argparse
import os
from pathlib import Path
import subprocess
import tempfile

parser = argparse.ArgumentParser()
for name in ('cpphdl', 'cxx', 'verilator'):
    parser.add_argument('--' + name, required=True)
args = parser.parse_args()
root = Path(__file__).resolve().parents[2]
source = Path(__file__).with_name('NamespaceConstants.cc')
env = dict(os.environ)
env['LD_LIBRARY_PATH'] = str(Path(args.cxx).resolve().parent.parent / 'lib') + ':' + env.get('LD_LIBRARY_PATH', '')
with tempfile.TemporaryDirectory(prefix='cpphdl-namespace-constants-') as directory:
    work = Path(directory)
    def run(command):
        result = subprocess.run(list(map(str, command)), env=env, text=True,
                                stdout=subprocess.PIPE, stderr=subprocess.STDOUT, timeout=90)
        if result.returncode:
            raise RuntimeError(result.stdout)
        return result.stdout
    run([args.cxx, '-std=c++17', '-I' + str(root / 'include'), source, '-o', work / 'native'])
    print(run([work / 'native']))
    generated = work / 'generated'
    run([args.cpphdl, '--generated-dir', generated, source, '--', '-I' + str(root / 'include')])
    rtl = (generated / 'NamespaceConstants.sv').read_text()
    assert 'OFFSET' in rtl and 'BITS +' in rtl, 'module parameter expression was prematurely substituted'
    assert 'Constants::' not in rtl and 'ConstantPolicy_pkg' not in rtl
    files = sorted(generated.glob('*_pkg.sv')) + sorted(p for p in generated.glob('*.sv') if not p.name.endswith('_pkg.sv'))
    run([args.verilator, '--cc', '--exe', '--build', '-j', '2', '-Wno-fatal',
         '--top-module', 'NamespaceConstants', '-GBITS=19', '--Mdir', work / 'obj',
         '-CFLAGS', '-std=c++17 -DVERILATOR -I' + str(root / 'include'),
         '-MAKEFLAGS', 'CXX=' + args.cxx + ' LINK=' + args.cxx +
         ' LDFLAGS=-L' + str(Path(args.cxx).resolve().parent.parent / 'lib'), *files, source])
    print(run([work / 'obj/VNamespaceConstants']))
