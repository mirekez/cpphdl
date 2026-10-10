"""Port bindings must convert to the declared payload before graph operations."""
import argparse
from pathlib import Path
import subprocess
import tempfile
import sys

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))
from TestToolchain import TestToolchain


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--cpphdl', required=True)
    parser.add_argument('--cxx', required=True)
    parser.add_argument('--work', type=Path, required=True)
    parser.add_argument('--verilator')
    args = parser.parse_args()
    fixture = Path(__file__).resolve().parent
    include = fixture.parents[1] / 'include'
    args.work.mkdir(parents=True, exist_ok=True)
    work = Path(tempfile.mkdtemp(prefix='port-binding-', dir=args.work))

    toolchain = TestToolchain(args.cxx, args.verilator, work)

    def run(command, label):
        result = subprocess.run(toolchain.command(command), cwd=work, text=True, env=toolchain.env,
                                stdout=subprocess.PIPE, stderr=subprocess.STDOUT, timeout=180)
        (work / (label + '.log')).write_text(result.stdout)
        if result.returncode:
            raise RuntimeError(f'{label}: {result.stdout[-8000:]}\nArtifacts: {work}')
        return result.stdout

    source = fixture / 'PortBinding.cc'
    runner = fixture / 'PortBindingRun.cc'
    if args.verilator:
        generated = work / 'rtl'
        run([args.cpphdl, '--generated-dir=' + str(generated), source,
             '--', '-std=c++23', '-I' + str(include)], 'convert')
        run([args.verilator, '--cc', '--exe', '--build', '-j', '1', '-Wno-fatal',
             '--top-module', 'PortBinding', '--Mdir', work / 'obj',
             '-CFLAGS', '-std=c++23 -DPORT_BINDING_RTL -I' + str(include),
             generated / 'Predef_pkg.sv', generated / 'PortBindingChild.sv',
             generated / 'BindingArraySource.sv', generated / 'BindingArraySink.sv',
             generated / 'PortBinding.sv', runner], 'verilator')
        print(run([work / 'obj/VPortBinding'], 'rtl-run'), end='')
    else:
        run([args.cxx, '-std=c++23', '-O1', '-fsanitize=address,undefined',
             '-I' + str(include), runner, '-o', work / 'native'], 'native-build')
        print(run([work / 'native'], 'native-run'), end='')
        for name, definitions in (('integer', []), ('typed', ['-DPORT_BINDING_TYPED'])):
            output = work / name
            run([args.cpphdl, '--native-graph', '--top', 'cpphdl_top', '--cxx', args.cxx,
                 *['--frontend-flag=' + flag for flag in definitions],
                 '--runner', runner, '--output', output, source, '--',
                 '-I' + str(include), '-DPORT_BINDING_GRAPH', *definitions,
                 '-fsanitize=address,undefined'], name + '-graph')
            print(run([output / 'run'], name + '-run'), end='')
        output = work / 'copies'
        copy_source = fixture / 'PortCopy.cc'
        run([args.cpphdl, '--native-graph', '--top', 'cpphdl_top', '--cxx', args.cxx,
             '--output', output, copy_source, '--', '-I' + str(include)], 'copies-graph')
        run([args.cxx, '-std=c++23', '-O2', '-DCHECK_PORT_COPY', '-I' + str(include),
             '-I' + str(output), copy_source, '-o', output / 'check'], 'copies-build')
        print(run([output / 'check'], 'copies-run'), end='')
        output = work / 'records'
        record_source = fixture / 'NativeRecords.cc'
        run([args.cpphdl, '--native-graph', '--top', 'cpphdl_top', '--cxx', args.cxx,
             '--output', output, record_source, '--', '-I' + str(include)], 'records-graph')
        run([args.cxx, '-std=c++23', '-O2', '-DCHECK_NATIVE_RECORDS', '-I' + str(include),
             '-I' + str(output), record_source, '-o', output / 'check'], 'records-build')
        print(run([output / 'check'], 'records-run'), end='')
        for threads in (1, 3):
            output = work / ('assertions-' + str(threads))
            assertion_source = fixture / 'NativeAssertions.cc'
            run([args.cpphdl, '--native-graph', '--optimize-threads=' + str(threads),
                 '--top', 'cpphdl_top', '--cxx', args.cxx, '--output', output,
                 assertion_source, '--', '-I' + str(include)], 'assertions-graph-' + str(threads))
            run([args.cxx, '-std=c++23', '-O2', '-pthread', '-DCHECK_NATIVE_ASSERTIONS',
                 '-I' + str(include), '-I' + str(output), assertion_source, '-o', output / 'check'],
                'assertions-build-' + str(threads))
            print(run([output / 'check'], 'assertions-run-' + str(threads)), end='')
        for threads in (1, 4):
            output = work / ('float-' + str(threads))
            float_source = fixture / 'NativeFloat.cc'
            run([args.cpphdl, '--native-graph', '--optimize-threads=' + str(threads),
                 '--top', 'cpphdl_top', '--cxx', args.cxx, '--output', output,
                 float_source, '--', '-I' + str(include)], 'float-graph-' + str(threads))
            run([args.cxx, '-std=c++23', '-O2', '-pthread', '-DCHECK_NATIVE_FLOAT',
                 '-I' + str(include), '-I' + str(output), float_source, '-o', output / 'check'],
                'float-build-' + str(threads))
            print(run([output / 'check'], 'float-run-' + str(threads)), end='')
        for threads in (1, 4):
            output = work / ('memory-getters-' + str(threads))
            source = fixture / 'NativeMemoryGetters.cc'
            run([args.cpphdl, '--native-graph', '--optimize-threads=' + str(threads),
                 '--top', 'cpphdl_top', '--cxx', args.cxx, '--output', output,
                 source, '--', '-I' + str(include)], 'memory-getters-graph-' + str(threads))
            run([args.cxx, '-std=c++23', '-O2', '-pthread', '-DCHECK_MEMORY_GETTERS',
                 '-I' + str(include), '-I' + str(output), source, '-o', output / 'check'],
                'memory-getters-build-' + str(threads))
            print(run([output / 'check'], 'memory-getters-run-' + str(threads)), end='')
        output = work / 'control-locals'
        source = fixture / 'NativeControlLocals.cc'
        run([args.cpphdl, '--native-graph', '--top', 'cpphdl_top', '--cxx', args.cxx,
             '--output', output, source, '--', '-I' + str(include)], 'control-locals-graph')
        run([args.cxx, '-std=c++23', '-O2', '-DCHECK_CONTROL_LOCALS', '-I' + str(include),
             '-I' + str(output), source, '-o', output / 'check'], 'control-locals-build')
        print(run([output / 'check'], 'control-locals-run'), end='')
        rejected = subprocess.run([args.cpphdl, '--lower-cpp-graph', str(source),
            str(work / 'bad-control.graph'), 'cpphdl_top', '--', '-std=c++23',
            '-DCONTROL_LOCALS_BAD', '-I' + str(include)], text=True,
            stdout=subprocess.PIPE, stderr=subprocess.STDOUT, timeout=180)
        (work / 'bad-control.log').write_text(rejected.stdout)
        if not rejected.returncode or 'undriven bit:' not in rejected.stdout:
            raise AssertionError('A genuinely uninitialized local must still be rejected: ' + rejected.stdout)
        print('uninitialized local on a live return path rejected PASS')


if __name__ == '__main__':
    main()
