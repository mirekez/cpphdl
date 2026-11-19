#!/usr/bin/env python3
"""Check that only independent, unconditional external reads overlap."""
import argparse
import hashlib
import json
import os
from pathlib import Path
import subprocess


def main():
    parser = argparse.ArgumentParser()
    for name in ('cpphdl', 'cxx', 'verilator', 'work'):
        parser.add_argument('--' + name, type=Path, required=True)
    parser.add_argument('--patterns', nargs='+', type=int, default=list(range(1, 5)))
    args = parser.parse_args()
    for name in ('cpphdl', 'cxx', 'verilator', 'work'):
        setattr(args, name, getattr(args, name).resolve())
    tests = Path(__file__).resolve().parent
    root = tests.parents[1]
    source = tests / 'ConcurrentReadsSafety.cpp'
    fixture = tests / 'ConcurrentReadsSafetyRun.cpp'
    args.work.mkdir(parents=True, exist_ok=True)
    report = {'status': 'running', 'cases': [], 'commands': [],
              'source_sha256': {str(f): hashlib.sha256(f.read_bytes()).hexdigest()
              for f in (source, fixture, root / 'hls/delayed_scheduler.cpp',
                        root / 'hls/Clocked.h', root / 'hls/ExternalMemory.h',
                        root / 'synth/retiming.cpp')}}
    def save():
        (args.work / 'results.json').write_text(json.dumps(report, indent=2) + '\n')
    def run(command, log, expected_return=0):
        command = [str(x) for x in command]
        report['commands'].append(command)
        save()
        env = os.environ.copy()
        env['PATH'] = str(args.verilator.parent) + os.pathsep + env.get('PATH', '')
        env['LD_LIBRARY_PATH'] = str(root / '.conda/lib') + os.pathsep + env.get('LD_LIBRARY_PATH', '')
        with log.open('w') as out:
            proc = subprocess.run(command, stdout=out, stderr=subprocess.STDOUT,
                                  timeout=900, env=env)
        result = log.read_text()
        if proc.returncode != expected_return:
            raise RuntimeError(f'{log} exited {proc.returncode}:\n{result[-3000:]}')
        return result
    try:
        for pattern in args.patterns:
            case = args.work / f'pattern{pattern}'
            if case.exists():
                for number in range(1, 1000):
                    older = args.work / f'pattern{pattern}-previous-{number}'
                    if not older.exists():
                        case.rename(older)
                        break
            case.mkdir(exist_ok=True)
            defines = [f'-DREAD_PATTERN={pattern}']
            native = case / 'native'
            run([args.cxx, '-std=c++17', '-O1', '-I' + str(root / 'include'),
                 *defines, source, '-o', native], case / 'native-build.log')
            if 'PASS native' not in run([native], case / 'native-run.log'):
                raise AssertionError('native marker absent')
            raw = case / 'raw'
            raw.mkdir(exist_ok=True)
            generated = run([args.cpphdl, '--generated-dir', raw, source, '--', '-std=c++17',
                 '-DSYNTHESIS', *defines, '-I' + str(root / 'include')],
                case / 'raw-generate.log', 1 if pattern == 1 else 0)
            if pattern == 1:
                if 'volatile/atomic access is not supported' not in generated:
                    raise AssertionError('volatile first read was not rejected precisely')
                report['cases'].append({'pattern': pattern, 'kind': 'rejected-volatile',
                                        'result': generated.strip()})
                save()
                continue
            sv = list(raw.glob('cpphdl_hls_ClockedMemory*.sv'))
            if len(sv) != 1:
                raise AssertionError(f'expected one raw module: {sv}')
            for kind, rtl, top in (('raw', sv[0], sv[0].stem),
                                   ('retimed', case / 'mapped/operations.v', 'ReadSafetyTop')):
                if kind == 'retimed':
                    mapped = case / 'mapped'
                    mapped.mkdir(exist_ok=True)
                    run([args.cpphdl, '--synth', '--top', 'worker', '--module', 'ReadSafetyTop',
                         '--cxx', args.cxx, '--output', mapped, '--retiming',
                         'fit_pipeline_retiming', '--clock-period-ns', '4', '--tool-timeout',
                         '1200', source, '--', '-std=c++17', '-DSYNTHESIS', *defines,
                         '-I' + str(root / 'include')], case / 'synth.log')
                    timing = json.loads((mapped / 'timing.json').read_text())
                    if not timing['rules'][0]['target_met']:
                        raise AssertionError(f'retiming target missed: {timing}')
                obj = case / (kind + '-obj')
                obj.mkdir(exist_ok=True)
                flags = f'-std=c++17 -O1 -DREAD_PATTERN={pattern}'
                if kind == 'retimed':
                    flags += ' -DREAD_RETIMED'
                build = run([args.verilator, '--cc', '--exe', '--build', '-j', '2',
                             '-Wno-fatal', '--top-module', top, '--prefix', 'VReadSafetyTop',
                             '--Mdir', obj, '-CFLAGS', flags, '-MAKEFLAGS',
                             f'CXX={args.cxx} LINK={args.cxx}', rtl, fixture],
                            case / (kind + '-build.log'))
                if '%Warning-LATCH' in build:
                    raise AssertionError(f'{kind}: latch warning')
                result = run([obj / 'VReadSafetyTop'], case / (kind + '-run.log'))
                if f'PASS safety pattern={pattern}' not in result:
                    raise AssertionError(f'{kind}: pass marker absent: {result}')
                report['cases'].append({'pattern': pattern, 'kind': kind,
                                        'result': result.strip()})
                save()
        report['status'] = 'passed'
        save()
    except BaseException as error:
        report['status'] = 'failed'
        report['error'] = str(error)
        save()
        raise


if __name__ == '__main__':
    main()
