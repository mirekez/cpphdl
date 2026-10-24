"""Elaborate template methods without explicit instantiations or C++ call sites."""
import argparse
from pathlib import Path
import subprocess
import sys

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))
from TestToolchain import TestToolchain

parser = argparse.ArgumentParser(description=__doc__)
for name in ('cpphdl', 'cxx', 'verilator', 'work'):
    parser.add_argument('--' + name, type=Path, required=True)
args = parser.parse_args()
source = Path(__file__).resolve().parent
repo = source.parents[1]
work = args.work.resolve()
work.mkdir(parents=True, exist_ok=True)
toolchain = TestToolchain(args.cxx.absolute(), args.verilator.resolve(), work)


def run(name, command):
    result = subprocess.run(toolchain.command(command), env=toolchain.env,
                            capture_output=True, text=True, timeout=120)
    (work / (name + '.log')).write_text(result.stdout + result.stderr)
    if result.returncode:
        raise RuntimeError(name + ':\n' + result.stdout + result.stderr)


run('native-build', [args.cxx, '-std=c++17', '-I' + str(repo / 'include'),
                     source / 'TemplateRootInstanceRun.cc', '-o', work / 'native'])
run('native', [work / 'native'])
for variant in ('scalar', 'array'):
    rtl = work / variant
    run(variant + '-convert', [args.cpphdl.resolve(), '--generated-dir', rtl,
        source / 'TemplateRootInstance.h', '--', '-I' + str(repo / 'include'),
        *(['-DROOT_INSTANCE_ARRAY'] if variant == 'array' else [])])
    run(variant + '-build', [args.verilator.resolve(), '--cc', '--exe', '--build',
        '-j', '1', '-Wno-fatal', '--top-module', 'RootInstanceTop', '--Mdir', rtl / 'obj',
        '-CFLAGS', '-DVERILATOR -std=c++17', rtl / 'Predef_pkg.sv',
        rtl / 'RootInstanceLeaf.sv', rtl / 'RootInstanceTop.sv',
        source / 'TemplateRootInstanceRun.cc'])
    run(variant + '-run', [rtl / 'obj/VRootInstanceTop'])
    print(variant + ': ' + (work / (variant + '-run.log')).read_text(), end='')
