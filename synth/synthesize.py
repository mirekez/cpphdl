#!/usr/bin/env python3
"""Lower ordinary CppHDL through the shared graph and map to generic gates."""
import argparse
import json
import os
from pathlib import Path
import re
import shutil
import subprocess
import sys

sys.path.insert(0, str(Path(__file__).resolve().parent.parent / 'tools'))
from graph_clocks import add_clock_arguments, clock_arguments


def main():
    arguments = sys.argv[1:]
    flags = []
    if '--' in arguments:
        split = arguments.index('--')
        arguments, flags = arguments[:split], arguments[split + 1:]
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--cpphdl', required=True)
    parser.add_argument('--top', required=True, help='C++ root object variable')
    parser.add_argument('--module', default='SynthTop', help='output Verilog module')
    parser.add_argument('--output', required=True, type=Path, help='new or empty directory')
    parser.add_argument('--cxx', default=os.environ.get('CXX', 'c++'))
    parser.add_argument('--yosys', default=os.environ.get('YOSYS', 'yosys'))
    parser.add_argument('--retiming', choices=('keep_behaviour_retiming', 'fit_pipeline_retiming'))
    parser.add_argument('--clock-period-ns', type=float, help='estimated timing target for retiming')
    parser.add_argument('--retime-module', default='', help='C++ instance path; default is the whole design')
    parser.add_argument('--delay-scale', type=float, default=1.0, help='scale all built-in delay estimates')
    parser.add_argument('source', type=Path)
    add_clock_arguments(parser)
    args = parser.parse_args(arguments)
    clocks = clock_arguments(parser, args)
    import math
    if args.retiming and (args.clock_period_ns is None or not math.isfinite(args.clock_period_ns) or args.clock_period_ns <= 0):
        parser.error('--retiming requires positive --clock-period-ns')
    if not args.retiming and (args.clock_period_ns is not None or args.retime_module):
        parser.error('--clock-period-ns/--retime-module require --retiming')
    if not math.isfinite(args.delay_scale) or args.delay_scale <= 0:
        parser.error('--delay-scale must be positive')
    if not re.fullmatch(r'[A-Za-z_][A-Za-z_0-9]*', args.module):
        parser.error('invalid output module name')
    output = args.output.resolve()
    if output.exists() and (not output.is_dir() or any(output.iterdir())):
        parser.error('output must be a new or empty directory')
    root = Path(__file__).resolve().parent.parent
    manifest = {'backend': 'cpphdl-synth', 'status': 'building', 'commands': [],
                'clock_contract': 'named clock edges' if clocks else 'one rising clk edge per work/strobe transaction',
                'clocks': clocks,
                'mapping': 'generic gates, no physical timing guarantee'}
    try:
        source = args.source.resolve(strict=True)
        compiler = shutil.which(args.cxx)
        yosys = shutil.which(args.yosys)
        if not compiler or not yosys:
            raise RuntimeError('C++ compiler and Yosys must both be available')
        cpphdl = str(Path(args.cpphdl).resolve(strict=True))
        output.mkdir(parents=True, exist_ok=True)

        def save():
            (output / 'manifest.json').write_text(json.dumps(manifest, indent=2) + '\n')

        def run(command, label, cwd=output):
            command = list(map(str, command))
            manifest['commands'].append(command)
            save()
            with (output / (label + '.log')).open('w') as log:
                result = subprocess.run(command, cwd=cwd, stdout=log, stderr=log, timeout=300)
            if result.returncode:
                raise RuntimeError(f'{label} failed: see {output / (label + ".log")}')

        run([cpphdl, '--lower-cpp-graph', source, output / 'graph.cc', args.top, *clocks,
             '--', '-std=c++17', '-I' + str(root / 'include'), *flags], 'lower', cwd=Path.cwd())
        run([compiler, '-std=c++17', '-O1', '-DCPPHDL_GRAPH_NO_MAIN', '-DCPPHDL_GRAPH_CORE_ONLY',
             '-I' + str(root / 'include'), output / 'graph.cc', root / 'synth/Main.cpp',
             root / 'synth/Verilog.cpp', root / 'synth/timing.cpp', root / 'synth/retiming.cpp', '-o', output / 'emit'], 'compile-emitter')
        run([output / 'emit', output / 'operations.v', args.module, output / 'timing.json',
             output / 'retimed_graph.cc', args.retiming or 'none', str(args.clock_period_ns or 0),
             args.retime_module, str(args.delay_scale)], 'emit')
        manifest['timing'] = json.loads((output / 'timing.json').read_text())
        boxes = {box['module'] for box in manifest['timing']['keep_boxes']}
        # Hide only explicitly preserved bodies during generic gate mapping.
        # Restore their real implementations afterwards, not unresolved blackboxes.
        preserve = ''.join(f'blackbox {name}\n' for name in sorted(boxes))
        script = (f'read_verilog operations.v\nhierarchy -check -top {args.module}\n'
                  + preserve +
                  f'synth -top {args.module}\ncheck -assert\n'
                  'write_json gates.json\nwrite_verilog -noattr gates.v\nstat\n')
        (output / 'synth.ys').write_text(script)
        run([yosys, '-s', 'synth.ys'], 'yosys')
        design = json.loads((output / 'gates.json').read_text())
        cells = design['modules'][args.module].get('cells', {})
        counts = {}
        for cell in cells.values():
            kind = cell['type']
            if not kind.startswith('$_') and kind not in boxes:
                raise RuntimeError(f'unmapped cell remains: {kind}')
            counts[kind] = counts.get(kind, 0) + 1
        if boxes:
            with (output / 'gates.v').open('a') as gates:
                gates.write('\n' + (output / 'keep_boxes.v').read_text())
            manifest['mapping'] = 'generic gates with preserved operation-level modules for technology mapping'
        manifest.update(status='complete', cells=counts)
        save()
        print(output / 'gates.v')
        return 0
    except (OSError, RuntimeError, subprocess.TimeoutExpired) as error:
        manifest.update(status='failed', error=str(error))
        if output.is_dir():
            (output / 'manifest.json').write_text(json.dumps(manifest, indent=2) + '\n')
        print(f'CppHDL synthesis: {error}', file=sys.stderr)
        return 1


if __name__ == '__main__':
    sys.exit(main())
