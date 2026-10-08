#!/usr/bin/env python3
import argparse
import json
import os
from pathlib import Path
import shutil
import subprocess

def main():
    p = argparse.ArgumentParser()
    for arg in ('cpphdl', 'cxx', 'verilator', 'work', 'case'):
        p.add_argument('--' + arg, required=True)
    args = p.parse_args()
    root = Path(__file__).resolve().parents[3]
    source = Path(__file__).with_name(args.case + '.cpp')
    base = Path(args.work).resolve()
    env = os.environ.copy()
    lib = Path(args.cxx).resolve().parent.parent / 'lib'
    env['LD_LIBRARY_PATH'] = str(lib) + ':' + env.get('LD_LIBRARY_PATH', '')
    modes = ['keep_behaviour_retiming', 'fit_pipeline_retiming']
    if args.case == 'logic_retiming': modes += ['annotated', 'scoped']
    if args.case == 'keep_box': modes += ['fast_box']
    reports = {}
    for mode in modes:
        work = base / mode
        if work.exists(): shutil.rmtree(work)
        work.mkdir(parents=True)
        def run(command, label):
            result = subprocess.run(list(map(str, command)), cwd=work, env=env, text=True,
                                    stdout=subprocess.PIPE, stderr=subprocess.STDOUT, timeout=300)
            (work / (label + '.log')).write_text(result.stdout)
            if result.returncode:
                print(result.stdout[-7000:])
                for log in (work / 'rtl').glob('*.log'): print(log.name, log.read_text()[-4000:])
                raise RuntimeError(label + ' failed')
        flags = ['--retiming', 'fit_pipeline_retiming' if mode in ('scoped', 'fast_box') else mode,
                 '--clock-period-ns', '2.5'] if mode != 'annotated' else []
        if mode == 'scoped': flags += ['--retime-module', 'cpphdl_top.stage']
        source_flags = ['-DRETIMING_ANNOTATE'] if mode == 'annotated' else []
        if mode == 'fast_box': source_flags += ['-DBOX_DELAY_NS="0.2"']
        run([args.cpphdl, '--synth', '--top', 'cpphdl_top', '--module', 'SynthRetiming', '--output', work / 'rtl',
             '--cxx', args.cxx, *flags, source, '--', *source_flags], 'synth')
        report = json.loads((work / 'rtl/timing.json').read_text())['rules'][0]
        print(mode, report, flush=True)
        reports[mode] = report
        if report['after_ns'] >= report['before_ns'] and args.case != 'keep_box': raise RuntimeError('timing did not improve')
        if args.case == 'keep_box':
            manifest = json.loads((work / 'rtl/manifest.json').read_text())
            boxes = manifest['timing']['keep_boxes']
            if len(boxes) != 1 or boxes[0]['delay_ns'] != (0.2 if mode == 'fast_box' else 2.1):
                raise RuntimeError('declared box timing was lost')
            body = (work / 'rtl/keep_boxes.v').read_text()
            if body.count(' * ') != 1 or body.count(' + ') != 1 or 'always' in body:
                raise RuntimeError('box implementation changed or acquired a register')
            if body not in (work / 'rtl/gates.v').read_text(): raise RuntimeError('box implementation missing from mapped design')
            mapped = json.loads((work / 'rtl/gates.json').read_text())['modules']['SynthRetiming']['cells']
            if sum(c['type'] == boxes[0]['module'] for c in mapped.values()) != 1:
                raise RuntimeError('box instance was flattened or duplicated')
        if mode == 'keep_behaviour_retiming':
            if (not report['moved_boundaries'] and args.case != 'keep_box') or report['added_latency']:
                raise RuntimeError('no behaviour-preserving move')
            # Both the retimed graph and mapped gates are checked against the
            # original C++ cycle-level oracle below; no external mapper is used.
        elif not report['target_met'] or not report['added_latency']:
            raise RuntimeError('pipeline did not meet target')
        definitions = ['-DRETIMING_RUN', '-DRETIMING_LATENCY=' + str(report['added_latency']),
                       '-DRETIMING_SCOPED=' + str(int(mode in ('annotated', 'scoped')))]
        run([args.cxx, '-std=c++17', '-O1', '-I' + str(root / 'include'), work / 'rtl/retimed_graph.cc', '-o', work / 'emit-native'], 'native-compiler')
        run([work / 'emit-native', work / 'model.h'], 'native-emit')
        run([args.cxx, '-std=c++17', '-O1', '-I' + str(root / 'include'), '-I' + str(work),
             *definitions, '-DRETIMING_GRAPH', source, '-o', work / 'native'], 'native-build')
        run([work / 'native'], 'native-test')
        run([args.verilator, '--cc', '--exe', '--build', '-j', '2', '--top-module', 'SynthRetiming', '-Wno-fatal',
             '--Mdir', work / 'obj', work / 'rtl/gates.v', source, '-CFLAGS',
             ' '.join(['-std=c++17', '-I' + str(root / 'include'), *definitions]),
             '-MAKEFLAGS', f'CXX={args.cxx} LINK={args.cxx} AR=ar LDFLAGS=-L{lib}'], 'verilator-build')
        run([work / 'obj/VSynthRetiming'], 'gate-test')
    if args.case == 'keep_box':
        if reports['fit_pipeline_retiming']['added_latency'] <= reports['fast_box']['added_latency']:
            raise RuntimeError('large box delay did not reduce available stage slack')

if __name__ == '__main__': main()
