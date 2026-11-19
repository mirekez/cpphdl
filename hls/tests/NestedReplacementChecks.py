#!/usr/bin/env python3
"""Check annotated atomic calls nested inside ordinary HLS methods."""
import argparse
import json
import os
import re
from pathlib import Path
import shutil
import subprocess


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--cpphdl', type=Path, required=True)
    parser.add_argument('--cxx', type=Path, required=True)
    parser.add_argument('--verilator', type=Path, required=True)
    parser.add_argument('--work', type=Path, required=True)
    args = parser.parse_args()
    tests = Path(__file__).resolve().parent
    root = tests.parents[1]
    work = args.work.resolve()
    work.mkdir(parents=True, exist_ok=True)
    report = {'status': 'running', 'cases': [], 'commands': []}

    def save():
        (work / 'results.json').write_text(json.dumps(report, indent=2) + '\n')

    def run(command, log, expect_ok=True):
        command = [str(x) for x in command]
        report['commands'].append(command)
        save()
        environment = os.environ.copy()
        environment['PATH'] = (str(args.verilator.resolve().parent) + os.pathsep +
                               str(args.cxx.resolve().parent) + os.pathsep +
                               environment.get('PATH', ''))
        runtime = subprocess.check_output([str(args.cxx), '-print-file-name=libatomic.so.1'], text=True).strip()
        runtime_dir = Path(runtime).resolve().parent if Path(runtime).exists() else args.cxx.resolve().parent.parent / 'lib'
        environment['LD_LIBRARY_PATH'] = (str(runtime_dir) + os.pathsep +
                                           environment.get('LD_LIBRARY_PATH', ''))
        with log.open('w') as out:
            process = subprocess.run(command, stdout=out, stderr=subprocess.STDOUT, env=environment)
        if (process.returncode == 0) != expect_ok:
            raise RuntimeError(f'{log}: exit {process.returncode}\n{log.read_text()[-4000:]}')
        return log.read_text()

    def fresh(name):
        directory = work / name
        if directory.exists() and any(directory.iterdir()):
            for index in range(1, 1000):
                preserved = work / f'{name}-previous-{index}'
                if not preserved.exists():
                    directory.rename(preserved)
                    break
            else:
                raise RuntimeError('too many preserved attempts')
        directory.mkdir(exist_ok=True)
        return directory

    try:
        for name, source, expected, count in (
            ('integer', 'NestedPipelineReplacement.cpp', 'MultiplyAddReplacement', 2),
            ('fp32', 'NestedFp32Pipeline.cpp', 'Fp32MacPipeline', 1),
        ):
            directory = fresh(name)
            native = work / f'{name}-native'
            run([args.cxx, '-std=c++17', '-O1', '-ffp-contract=off', '-DPIPELINE_STAGES=3',
                 '-I' + str(root / 'include'), tests / source, '-o', native],
                work / f'{name}-native-build.log')
            run([native], work / f'{name}-native-run.log')
            ordinary = fresh(f'{name}-ordinary')
            run([args.cpphdl, '--generated-dir', ordinary, tests / source, '--',
                 '-std=c++17', '-DPIPELINE_STAGES=3', '-I' + str(root),
                 '-I' + str(root / 'include')], work / f'{name}-ordinary.log')
            ordinary_rtl = '\n'.join(path.read_text() for path in ordinary.glob('*.sv'))
            if ordinary_rtl.count(f'\\{expected} #(') != count or not re.search(
                r'HLS pipelined_logic: II=1, STAGES=3, LATENCY=[1-9][0-9]*', ordinary_rtl
            ):
                raise AssertionError(f'{name}: ordinary generated RTL lost actual latency or atomic instances')
            run([args.cpphdl, '--synth', '--top', 'PipelineTop', '--module', 'PipelineTop',
                 '--cxx', args.cxx, '--output', directory, '--retiming', 'fit_pipeline_retiming',
                 '--clock-period-ns', '4', '--tool-timeout', '1200', tests / source, '--',
                 '-std=c++17', '-DSYNTHESIS', '-DPIPELINE_STAGES=3', '-ffp-contract=off',
                 '-I' + str(root), '-I' + str(root / 'include')], work / f'{name}-synth.log')
            timing = json.loads((directory / 'timing.json').read_text())
            rule = timing['rules'][0]
            regions = timing['streaming_regions']
            replacements = [item for item in timing['pipeline_replacements'] if item['module'] == expected]
            if not rule['target_met'] or rule['initiation_interval'] != 1 or len(replacements) != count:
                raise AssertionError(f'{name}: missing actual retimed atomic calls: {timing}')
            latency = regions[0]['latency']
            if latency < 3 or any(item['latency'] < 2 for item in replacements):
                raise AssertionError(f'{name}: invalid selected latency: {timing}')
            gates = (directory / 'gates.v').read_text()
            if gates.count(expected) < count or any(
                f'.LATENCY({item["latency"]})' not in gates for item in replacements
            ):
                raise AssertionError(f'{name}: replacement instances missing from mapped RTL')
            flags = ('-std=c++17 -O1 -ffp-contract=off -DVERILATOR -DPIPELINE_STAGES=3 '
                     f'-DPIPELINE_LATENCY={latency} -I{root} -I{root / "include"}')
            obj = directory / 'obj'
            run([args.verilator, '--cc', '--exe', '--build', '-j', '2', '-Wno-fatal',
                 *(['-DSYNTHESIS'] if name == 'fp32' else []),
                 '--top-module', 'PipelineTop', '--prefix', 'VPipelineTop', '--Mdir', obj,
                 '-CFLAGS', flags, '-MAKEFLAGS', f'CXX={args.cxx} LINK={args.cxx}',
                 directory / 'gates.v', tests / source], work / f'{name}-build.log')
            result = run([obj / 'VPipelineTop'], work / f'{name}-run.log')
            report['cases'].append({'name': name, 'latency': latency, 'replacements': replacements,
                                    'result': result.strip()})
            save()
        directory = fresh('memory')
        native = work / 'memory-native'
        run([args.cxx, '-std=c++17', '-O1', '-I' + str(root / 'include'),
             tests / 'NestedMemoryReplacement.cpp', '-o', native], work / 'memory-native-build.log')
        run([native], work / 'memory-native-run.log')
        ordinary = fresh('memory-ordinary')
        run([args.cpphdl, '--generated-dir', ordinary,
             tests / 'NestedMemoryReplacement.cpp', '--', '-std=c++17',
             '-I' + str(root), '-I' + str(root / 'include')], work / 'memory-ordinary.log')
        ordinary_rtl = '\n'.join(path.read_text() for path in ordinary.glob('*.sv'))
        if '\\MultiplyAddReplacement #(' not in ordinary_rtl:
            raise AssertionError('memory ordinary generated RTL lost atomic instance')
        run([args.cpphdl, '--synth', '--top', 'worker', '--module', 'NestedMemoryTop',
             '--cxx', args.cxx, '--output', directory, '--retiming', 'fit_pipeline_retiming',
             '--clock-period-ns', '4', '--tool-timeout', '1200',
             tests / 'NestedMemoryReplacement.cpp', '--', '-std=c++17', '-DSYNTHESIS',
             '-I' + str(root), '-I' + str(root / 'include')], work / 'memory-synth.log')
        timing = json.loads((directory / 'timing.json').read_text())
        replacements = [item for item in timing['pipeline_replacements']
                        if item['module'] == 'MultiplyAddReplacement']
        if len(replacements) != 1 or replacements[0]['latency'] < 2:
            raise AssertionError(f'memory nested atomic call lost replacement: {timing}')
        gates = (directory / 'gates.v').read_text()
        if f'.LATENCY({replacements[0]["latency"]})' not in gates:
            raise AssertionError('memory replacement latency differs from selected schedule')
        flags = f'-std=c++17 -O1 -I{root / "include"}'
        obj = directory / 'obj'
        run([args.verilator, '--cc', '--exe', '--build', '-j', '2', '-Wno-fatal',
             '--top-module', 'NestedMemoryTop', '--prefix', 'VNestedMemoryTop', '--Mdir', obj,
             '-CFLAGS', flags, '-MAKEFLAGS', f'CXX={args.cxx} LINK={args.cxx}',
             directory / 'gates.v', tests / 'NestedMemoryReplacementRun.cpp'],
            work / 'memory-build.log')
        result = run([obj / 'VNestedMemoryTop'], work / 'memory-run.log')
        report['cases'].append({'name': 'memory', 'replacements': replacements,
                                'result': result.strip()})
        save()
        for kind, phrase in ((1, 'state'), (2, 'recurs')):
            directory = fresh(f'reject-{kind}')
            output = run([args.cpphdl, '--synth', '--top', 'worker', '--module', 'Reject',
                          '--cxx', args.cxx, '--output', directory,
                          tests / 'NestedReplacementReject.cpp', '--', '-std=c++17',
                          '-DSYNTHESIS', f'-DREJECT_CASE={kind}', '-I' + str(root),
                          '-I' + str(root / 'include')], work / f'reject-{kind}.log', False)
            if phrase not in output.lower():
                raise AssertionError(f'unsupported annotated {phrase} call lacked a clear diagnostic')
        report['status'] = 'passed'
        save()
    except BaseException as error:
        report['status'] = 'failed'
        report['error'] = str(error)
        save()
        raise


if __name__ == '__main__':
    main()
