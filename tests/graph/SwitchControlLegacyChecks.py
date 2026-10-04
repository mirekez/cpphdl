"""Exercise one control-flow fixture in C++, native graph, and generated RTL."""
import argparse
from pathlib import Path
import subprocess
import tempfile
import sys

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))
from TestToolchain import TestToolchain


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument('--cpphdl', required=True)
    parser.add_argument('--cxx', required=True)
    parser.add_argument('--work', type=Path, required=True)
    parser.add_argument('--flow', choices=('cpp', 'graph', 'effects', 'verilator'), required=True)
    parser.add_argument('--verilator')
    args = parser.parse_args()
    fixture = Path(__file__).resolve().parent
    include = fixture.parents[1] / 'include'
    args.work.mkdir(parents=True, exist_ok=True)
    # Retain logs and generated artifacts on failure; each invocation is isolated.
    work = Path(tempfile.mkdtemp(prefix=args.flow + '-', dir=args.work))
    toolchain = TestToolchain(args.cxx, getattr(args, 'verilator', None), work)

    def run(command, label):
        result = subprocess.run(toolchain.command(command), cwd=work, env=toolchain.env, text=True,
                                stdout=subprocess.PIPE, stderr=subprocess.STDOUT, timeout=180)
        (work / (label + '.log')).write_text(result.stdout)
        if result.returncode:
            raise RuntimeError(f'{label} failed: {result.stdout[-6000:]}\nArtifacts: {work}')
        return result.stdout

    runner = fixture / 'SwitchControlRun.cpp'
    source = fixture / 'SwitchControl.cpp'
    if args.flow == 'cpp':
        run([args.cxx, '-std=c++23', '-O1', '-fsanitize=address,undefined',
             '-I' + str(include), runner, '-o', work / 'run'], 'compile')
        executable = work / 'run'
    elif args.flow in ('graph', 'effects'):
        flags = ['-DSWITCH_GRAPH']
        if args.flow == 'effects':
            source = runner = fixture / 'SwitchEffects.cpp'
            flags = ['-DSWITCH_EFFECTS_RUN', '-Wl,--wrap=random']
        run([args.cpphdl, '--native-graph', '--top', 'cpphdl_top', '--cxx', args.cxx,
             '--output', work / 'graph', '--runner', runner, source, '--', *flags,
             '-I' + str(include), '-fsanitize=address,undefined'], 'graph')
        executable = work / 'graph/run'
    else:
        run([args.cpphdl, '--generated-dir=' + str(work / 'rtl'), source,
             '--', '-std=c++23', '-I' + str(include)], 'convert')
        run([args.verilator, '--cc', '--exe', '--build', '-j', '1', '-Wno-fatal',
             '--top-module', 'SwitchControl', '--Mdir', work / 'obj',
             '-CFLAGS', '-std=c++23 -DSWITCH_VERILATOR -I' + str(include),
             work / 'rtl/Predef_pkg.sv', work / 'rtl/SwitchControl.sv', runner], 'verilator')
        executable = work / 'obj/VSwitchControl'
    print(run([executable], 'run'), end='')


if __name__ == '__main__':
    main()
