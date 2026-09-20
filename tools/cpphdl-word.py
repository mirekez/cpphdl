#!/usr/bin/env python3
"""Lower and build an authoritative C++ word model; never reads source RTL."""

import argparse
import hashlib
import json
import os
from pathlib import Path
import shutil
import subprocess
import sys


def main(arguments=None):
    arguments = list(sys.argv[1:] if arguments is None else arguments)
    flags = []
    if '--' in arguments:
        split = arguments.index('--')
        arguments, flags = arguments[:split], arguments[split + 1:]
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--cpphdl', required=True)
    parser.add_argument('--cxx', default=os.environ.get('CXX', 'clang++'))
    parser.add_argument('--runtime', type=Path, required=True)
    parser.add_argument('--output', type=Path, required=True)
    parser.add_argument('--runner', type=Path)
    parser.add_argument('--header', type=Path)
    parser.add_argument('source', type=Path)
    args = parser.parse_args(arguments)
    output = args.output.resolve()
    if output.exists() and (not output.is_dir() or any(output.iterdir())):
        parser.error('output must be a new or empty directory')
    manifest = {'backend': 'cpphdl-word', 'source_kind': 'authoritative-c++',
                'status': 'building', 'commands': []}
    try:
        source = args.source.resolve(strict=True)
        if source.suffix not in ('.cc', '.cpp', '.cxx'):
            raise ValueError('word backend requires C++ source, not RTL or a manifest')
        runtime = args.runtime.resolve(strict=True)
        if not (runtime / 'cxxrtl/cxxrtl.h').is_file():
            raise ValueError('runtime must contain cxxrtl/cxxrtl.h')
        compiler, optimizer = shutil.which(args.cxx), shutil.which(args.cpphdl)
        if not compiler or not optimizer:
            raise ValueError('C++ compiler and cpphdl must be existing executables')
        runner = args.runner.resolve(strict=True) if args.runner else None
        header = (args.header or source.with_suffix('.h')).resolve(strict=True) if runner or args.header else None
        include = Path(__file__).resolve().parent.parent / 'include'
        output.mkdir(parents=True, exist_ok=True)

        def digest(path):
            return hashlib.sha256(path.read_bytes()).hexdigest()

        inputs = [source, include / 'cpphdl_netlist_words.h', *runtime.rglob('*.h')]
        inputs += list(source.parent.glob('*.h'))
        if runner:
            inputs.append(runner)
        if header:
            inputs.append(header)
        manifest['inputs'] = {str(path): digest(path) for path in inputs}

        def save():
            (output / 'manifest.json').write_text(json.dumps(manifest, indent=2) + '\n')

        def run(command, label):
            manifest['commands'].append(command)
            save()
            with (output / (label + '.log')).open('w') as log:
                result = subprocess.run(command, cwd=output, stdout=log, stderr=log)
            if result.returncode:
                raise RuntimeError(f'{label} failed ({result.returncode}); see {output / (label + ".log")}')

        common = ['-std=c++23', '-I' + str(include), '-I' + str(source.parent),
                  '-I' + str(runtime), *flags]
        lowered = output / 'model.cc'
        run([optimizer, '--lower-word-model', str(source), str(lowered), '--', *common], 'lower')
        command = [compiler, *common, '-O2', '-c', str(lowered), '-o', str(output / 'model.o')]
        run(command, 'compile-model')
        artifacts = [lowered, output / 'model.o']
        if runner:
            # A runner beside an older model.h must not silently use another
            # class layout. Load this model's authoritative interface first.
            run([compiler, *common, '-include', str(header), '-O2', str(runner), str(output / 'model.o'),
                 '-o', str(output / 'run')], 'link-runner')
            artifacts.append(output / 'run')
        if manifest['inputs'] != {str(path): digest(path) for path in inputs}:
            raise RuntimeError('input files changed during word-model build')
        manifest['artifacts'] = {path.name: digest(path) for path in artifacts}
        manifest['status'] = 'complete'
        save()
        print(artifacts[-1])
        return 0
    except (OSError, ValueError, RuntimeError) as error:
        manifest['status'] = 'failed'
        manifest['error'] = str(error)
        if output.is_dir():
            (output / 'manifest.json').write_text(json.dumps(manifest, indent=2) + '\n')
        print(f'cpphdl-word: {error}', file=sys.stderr)
        return 1


if __name__ == '__main__':
    sys.exit(main())
