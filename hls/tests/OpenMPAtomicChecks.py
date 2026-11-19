#!/usr/bin/env python3
"""Exercise annotated, stateless atomic calls inside SIMD lanes."""
import argparse
import json
import os
import shutil
import subprocess
from pathlib import Path


def run(command, log, env=None):
    result = subprocess.run([str(value) for value in command], capture_output=True, text=True, env=env)
    log.write_text(result.stdout + result.stderr)
    if result.returncode:
        raise RuntimeError(f"{command[0]} failed ({result.returncode}); see {log}")


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument('--cpphdl', type=Path, required=True)
    parser.add_argument('--cxx', type=Path, required=True)
    parser.add_argument('--root', type=Path, required=True)
    parser.add_argument('--work', type=Path, required=True)
    args = parser.parse_args()
    root = args.root.resolve()
    work = args.work.resolve()
    work.mkdir(parents=True, exist_ok=True)
    source = root / 'hls/tests/OpenMPAtomic.cpp'
    native = work / 'native'
    run([args.cxx, '-std=c++17', '-O1', '-fopenmp', '-ffp-contract=off',
         '-DPIPELINE_STAGES=3', '-I' + str(root / 'include'),
         '-I' + str(root / 'hls/tests'), source, '-o', native], work / 'native-build.log')
    env = os.environ.copy()
    runtime_lib = root / '.conda/lib'
    env['LD_LIBRARY_PATH'] = str(runtime_lib) + ':' + env.get('LD_LIBRARY_PATH', '')
    run([native], work / 'native-run.log', env)
    generated = work / 'mapped'
    if generated.exists():
        shutil.rmtree(generated)
    run([args.cpphdl, '--synth', '--top', 'PipelineTop', '--module', 'PipelineTop',
         '--cxx', args.cxx, '--output', generated, '--retiming', 'fit_pipeline_retiming',
         '--clock-period-ns', '4', '--tool-timeout', '1200', source, '--',
         '-std=c++17', '-DSYNTHESIS', '-DPIPELINE_STAGES=3', '-fopenmp',
         '-I' + str(root / 'include'), '-I' + str(root / 'hls/tests')], work / 'synth.log')
    manifest = json.loads((generated / 'timing.json').read_text())
    replacements = [item for item in manifest['pipeline_replacements']
                    if item['module'] == 'MultiplyAddReplacement']
    rule = manifest['rules'][0]
    if len(replacements) != 4 or not rule['target_met'] or rule['initiation_interval'] != 1:
        raise AssertionError('SIMD atomic extraction or retiming failed')
    (work / 'result.json').write_text(json.dumps({'replacement_count': len(replacements),
        'ii': rule['initiation_interval'], 'target_met': rule['target_met']}, indent=2) + '\n')


if __name__ == '__main__':
    main()
