#!/usr/bin/env python3
"""Compare ordinary C++ and shared-graph simulation with mapped Verilog gates."""
import argparse
import os
from pathlib import Path
import shutil
import subprocess


def main():
    parser = argparse.ArgumentParser()
    for key in ('cpphdl', 'cxx', 'verilator', 'work'):
        parser.add_argument('--' + key, required=True)
    parser.add_argument('--case', choices=('math', 'pipeline', 'blackbox', 'interface_proxy', 'main_and_secondary', 'two_main_clocks', 'memory', 'async_reset', 'memory_async_reset'), default='math')
    args = parser.parse_args()
    root = Path(__file__).resolve().parents[2]
    work = Path(args.work).resolve()
    work.mkdir(parents=True, exist_ok=True)
    source = Path(__file__).with_name(args.case + '.cpp')
    module = 'Synth' + args.case.title()
    define = 'SYNTH_' + args.case.upper()
    clocks = []
    if args.case == 'blackbox':
        module = 'BlackBoxTest'
    if args.case in ('main_and_secondary', 'two_main_clocks', 'memory', 'async_reset', 'memory_async_reset'):
        source = Path(__file__).parent / 'multiclock' / (args.case + '.cpp')
        define = 'SYNTH_MULTICLOCK'
        if args.case == 'main_and_secondary':
            module = 'MainAndSecondary'
            clocks = ['--primary_clock', 'main_clk', '120000000', '--secondary_clock', 'secondary_clk', '40000000']
        elif args.case == 'two_main_clocks':
            module = 'TwoMainClocks'
            clocks = ['--clock', 'left_clk', '100000000', '--clock', 'right_clk', '60000000']
        elif args.case in ('memory', 'memory_async_reset'):
            module = 'MulticlockMemory'
            clocks = ['--clock', 'write_clk', '120000000', '--clock', 'read_clk', '40000000']
        else:
            module = 'AsynchronousReset'
            clocks = ['--clock', 'fast_clk', '120000000', '--clock', 'slow_clk', '40000000']
    # These are test-owned outputs, never source-tree generated files.
    for name in ('rtl', 'native', 'obj'):
        path = work / name
        if path.exists():
            shutil.rmtree(path)
    env = os.environ.copy()
    compiler_lib = Path(args.cxx).resolve().parent.parent / 'lib'
    if compiler_lib.is_dir():
        env['LD_LIBRARY_PATH'] = str(compiler_lib) + ':' + env.get('LD_LIBRARY_PATH', '')

    def run(command, label):
        print(' '.join(map(str, command)), flush=True)
        result = subprocess.run(list(map(str, command)), cwd=work, env=env,
                                stdout=subprocess.PIPE, stderr=subprocess.STDOUT, text=True, timeout=300)
        (work / (label + '.log')).write_text(result.stdout)
        if result.returncode:
            print(result.stdout[-12000:])
            for log in work.rglob('*.log'):
                if log.parent != work:
                    print(str(log) + '\n' + log.read_text()[-6000:])
            raise RuntimeError(f'{label}: exit {result.returncode}')
        print(result.stdout[-1000:], flush=True)

    run([args.cpphdl, '--synth', '--top', 'cpphdl_top', '--module', module,
         '--cxx', args.cxx, '--output', work / 'rtl', *clocks, source], 'synthesis')
    if args.case != 'blackbox':
      run([args.cpphdl, '--native-graph', '--top', 'cpphdl_top', '--cxx', args.cxx,
         '--output', work / 'native', '--runner', source, *clocks, source, '--',
         '-D' + define + '_RUN', '-D' + define + '_GRAPH', '-I' + str(root / 'include')], 'native-build')
      run([work / 'native/run'], 'native-test')
    extra_sources = [Path(__file__).with_name('BlackBoxModels.sv')] if args.case == 'blackbox' else []
    test_define = 'BLACKBOX_RUN' if args.case == 'blackbox' else define + '_RUN'
    run([args.verilator, '--cc', '--exe', '--build', '-j', '2', '--top-module', module,
         '-Wno-fatal', '--Mdir', work / 'obj', work / 'rtl/gates.v', *extra_sources, source,
         '-CFLAGS', f'-std=c++17 -D{test_define} -D{define}_VERILATOR -I{root / "include"}',
         '-MAKEFLAGS', f'CXX={args.cxx} LINK={args.cxx} AR=ar LDFLAGS=-L{compiler_lib}'], 'verilator-build')
    run([work / ('obj/V' + module)], 'gate-test')
    if args.case == 'blackbox':
        import json
        report = json.loads((work / 'rtl/manifest.json').read_text())
        assert set(report['external_implementations_required']) == {'opaque_a', 'opaque_b', 'opaque_c'}
        cells = json.loads((work / 'rtl/gates.json').read_text())['modules'][module]['cells']
        boxes = [cell for cell in cells.values() if cell['type'].startswith('opaque_')]
        assert sorted(cell['type'] for cell in boxes) == ['opaque_a', 'opaque_a', 'opaque_b', 'opaque_c']
        for cell in boxes:
            assert len(cell['connections']['args']) == 128 and len(cell['connections']['result']) == 16
        for case, message in {1: 'invalid blackbox delay', 2: 'invalid blackbox delay',
                              3: 'blackbox arguments must be scalar integers',
                              4: 'blackbox requires a free/static scalar integer function'}.items():
            target = work / f'rejected-{case}.cc'
            target.unlink(missing_ok=True)
            result = subprocess.run([args.cpphdl, '--lower-synthesis-graph', str(source), str(target), 'cpphdl_top',
                '--', '-I' + str(root / 'include'), f'-DBLACKBOX_ERROR={case}'],
                cwd=work, env=env, text=True, capture_output=True, timeout=60)
            if result.returncode == 0 or message not in result.stderr or target.exists():
                raise RuntimeError(f'blackbox error {case} was not rejected: ' + result.stderr)
    if clocks:
        import json
        design = json.loads((work / 'rtl/gates.json').read_text())['modules'][module]
        clock_bits = {design['ports'][name]['bits'][0] for name in clocks[1::3]}
        mapped_clocks = {cell['connections']['C'][0] for cell in design['cells'].values()
                         if 'C' in cell['connections'] and 'DFF' in cell['type']}
        if mapped_clocks != clock_bits:
            raise RuntimeError('mapped flop clocks do not match the declared clocks')
        rtl = (work / 'rtl/operations.v').read_text()
        for name in clocks[1::3]:
            if f'always @(posedge \\{name} ' not in rtl:
                raise RuntimeError('missing separate clock process: ' + name)
        if args.case == 'main_and_secondary' and 'always @(negedge \\main_clk ' not in rtl:
            raise RuntimeError('missing negative-edge process')
        if args.case == 'async_reset':
            if rtl.count('or posedge') != 3 or not any('DFF' in c['type'] and 'R' in c['connections']
                                                     for c in design['cells'].values()):
                raise RuntimeError('missing asynchronous reset in mapped flip-flops')
    if args.case in ('memory', 'async_reset'):
        diagnostics = ({1: 'memory write/apply clock mismatch', 2: 'memory write/apply clock mismatch',
                        3: 'multiple clock/edge writers', 4: 'cross-clock pending memory read'}
                       if args.case == 'memory' else
                       {1: 'missing asynchronous reset assignment', 2: 'unconditional constant value',
                        3: 'clock ownership mismatch', 4: 'asynchronous reset cannot access memory',
                        5: 'unconditional constant value'})
        for case, message in diagnostics.items():
            target = work / f'bad-event-{case}.cc'
            target.unlink(missing_ok=True)
            define_error = 'SYNTH_MEMORY_ERROR' if args.case == 'memory' else 'SYNTH_RESET_ERROR'
            result = subprocess.run([args.cpphdl, '--lower-cpp-graph', str(source), str(target), 'cpphdl_top',
                *clocks, '--', '-std=c++17', '-I' + str(root / 'include'), f'-D{define_error}={case}'],
                cwd=work, env=env, text=True, capture_output=True, timeout=60)
            if result.returncode == 0 or message not in result.stderr or target.exists():
                raise RuntimeError(f'bad-event-{case} was not rejected: ' + result.stderr)
    if args.case == 'main_and_secondary':
        diagnostics = {1: 'missing clock lifecycle pair', 2: 'multiple clock/edge owners',
                       3: 'multiple clock/edge owners', 4: 'named-clock',
                       5: 'dynamic structural/commit C++ condition', 6: 'cross-clock lifecycle call',
                       7: 'clock work/strobe mismatch', 8: 'cross-clock next-state read'}
        for case, message in diagnostics.items():
            target = work / f'bad-clock-{case}.cc'
            target.unlink(missing_ok=True)
            result = subprocess.run([args.cpphdl, '--lower-cpp-graph', str(source), str(target), 'cpphdl_top',
                '--clock', 'main_clk', '120000000', '--clock', 'secondary_clk', '40000000',
                '--', '-std=c++17', '-I' + str(root / 'include'), f'-DSYNTH_CLOCK_ERROR={case}'],
                cwd=work, env=env, text=True, capture_output=True, timeout=60)
            if result.returncode == 0 or message not in result.stderr or target.exists():
                raise RuntimeError(f'bad-clock-{case} was not rejected: ' + result.stderr)
        invalid_clocks = [
            (['--secondary_clock', 'secondary_clk', '10'], 'requires --primary_clock'),
            (['--primary_clock', 'main_clk', '10', '--secondary_clock', 'secondary_clk', '20'], 'highest frequency'),
            (['--clock', 'main_clk', '10', '--clock', 'main_clk', '10'], 'duplicate clock'),
            (['--clock', 'main_clk', '0'], 'positive'),
            (['--clock', 'main_clk', '10', '--primary_clock', 'secondary_clk', '20'], 'not both')]
        for flags, message in invalid_clocks:
            result = subprocess.run([args.cpphdl, '--synth', '--top', 'cpphdl_top', '--output', str(work / 'invalid'),
                *flags, str(source)], env=env, text=True, capture_output=True, timeout=30)
            if result.returncode == 0 or message not in result.stderr or (work / 'invalid').exists():
                raise RuntimeError('invalid clock CLI was not rejected: ' + result.stderr)
    if args.case == 'pipeline':
        target = work / 'bad-clock.cc'
        target.unlink(missing_ok=True)
        result = subprocess.run([args.cpphdl, '--lower-cpp-graph', str(source), str(target),
                                 'cpphdl_top', '--', '-std=c++17', '-I' + str(root / 'include'),
                                 '-DSYNTH_BAD_CLOCK'], cwd=work, env=env, text=True,
                                capture_output=True, timeout=60)
        if result.returncode == 0 or 'named-clock' not in result.stderr or target.exists():
            raise RuntimeError('unsupported clock was not rejected: ' + result.stderr)


if __name__ == '__main__':
    main()
