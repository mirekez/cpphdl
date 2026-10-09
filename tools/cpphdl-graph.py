#!/usr/bin/env python3
"""Compile an authoritative CppHDL value graph; no HDL or external synthesis tool."""

import argparse
import hashlib
import json
import os
from pathlib import Path
import shutil
import shlex
import subprocess
import sys
from graph_clocks import add_clock_arguments, clock_arguments


def main(arguments=None):
    arguments = list(sys.argv[1:] if arguments is None else arguments)
    flags = []
    if '--' in arguments:
        index = arguments.index('--')
        arguments, flags = arguments[:index], arguments[index + 1:]
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--cpphdl')
    parser.add_argument('--output', required=True, type=Path)
    parser.add_argument('--cxx', default=os.environ.get('CXX', 'clang++'))
    parser.add_argument('--runner', type=Path)
    parser.add_argument('--optimize-threads', type=int, default=1,
                        help='native evaluation lanes, including the caller (1..256; default: 1)')
    parser.add_argument('--top', help='root variable in ordinary CppHDL C++ (no hdlcpp graph mode)')
    parser.add_argument('--frontend-flag', action='append', default=[], help='C++ parsing flag; use --frontend-flag=-I/path')
    parser.add_argument('source', type=Path)
    add_clock_arguments(parser)
    args = parser.parse_args(arguments)
    if not 1 <= args.optimize_threads <= 256:
        parser.error('--optimize-threads must be between 1 and 256')
    clocks = clock_arguments(parser, args)
    if clocks and not args.top:
        parser.error('clock declarations require --top for ordinary CppHDL lowering')
    output = args.output.resolve()
    if output.exists() and (not output.is_dir() or any(output.iterdir())):
        parser.error('output must be a new or empty directory')
    manifest = {'backend': 'cpphdl-native-graph', 'status': 'building', 'commands': [],
                'optimize_threads': args.optimize_threads}
    try:
        source = args.source.resolve(strict=True)
        if source.suffix not in ('.cc', '.cpp', '.cxx'):
            raise ValueError('native graph requires authoritative C++ source')
        compiler = shutil.which(args.cxx)
        if not compiler:
            raise ValueError('C++ compiler not found')
        runner = args.runner.resolve(strict=True) if args.runner else None
        include = Path(__file__).resolve().parent.parent / 'include'
        inputs = [source, include / 'cpphdl_graph.h', include / 'cpphdl_graph_native.h',
                  include / 'cpphdl_graph_threads.h'] + ([runner] if runner else [])
        digest = lambda path: hashlib.sha256(path.read_bytes()).hexdigest()
        manifest['inputs'] = {str(path): digest(path) for path in inputs}
        output.mkdir(parents=True, exist_ok=True)

        def save():
            (output / 'manifest.json').write_text(json.dumps(manifest, indent=2) + '\n')

        def run(command, label):
            manifest['commands'].append(command)
            save()
            with (output / (label + '.log')).open('w') as log:
                result = subprocess.run(command, cwd=output, stdout=log, stderr=log)
            if result.returncode:
                raise RuntimeError(f'{label} failed (status {result.returncode}): see {output / (label + ".log")}')

        graph_source = source
        if args.top:
            if not args.cpphdl:
                raise ValueError('--top requires the cpphdl executable')
            graph_source = output / 'graph.cc'
            dependencies = output / 'dependencies.d'
            run([compiler, '-std=c++23', '-I' + str(include), *args.frontend_flag,
                 '-MM', '-MT', 'cppgraph', '-MF', str(dependencies), str(source)], 'dependencies')
            dependency_text = dependencies.read_text().replace('\\\n', '')
            headers = shlex.split(dependency_text.split(':', 1)[1])
            inputs = list(dict.fromkeys([*inputs, Path(args.cpphdl).resolve(strict=True),
                                        *[(output / path).resolve(strict=True) for path in headers]]))
            manifest['inputs'] = {str(path): digest(path) for path in inputs}
            run([str(Path(args.cpphdl).resolve()), '--lower-cpp-graph', str(source),
                 str(graph_source), args.top, *clocks, '--', '-std=c++23', '-I' + str(include),
                 *args.frontend_flag], 'cpp-to-graph')
        run([compiler, '-std=c++23', '-O1', '-I' + str(include),
             '-DCPPHDL_NATIVE_THREADS=' + str(args.optimize_threads), str(graph_source),
             '-o', str(output / 'lower')], 'build-graph-compiler')
        run([str(output / 'lower'), str(output / 'model.h')], 'lower')
        if runner:
            run([compiler, '-std=c++23', '-O2', '-I' + str(output), '-I' + str(include),
                 *(['-pthread'] if args.optimize_threads > 1 else []),
                 '-include', str(output / 'model.h'), *flags, str(runner),
                 '-o', str(output / 'run')], 'build-runner')
        if manifest['inputs'] != {str(path): digest(path) for path in inputs}:
            raise RuntimeError('inputs changed during build')
        manifest['status'] = 'complete'
        manifest['artifacts'] = {path.name: digest(path) for path in
                                 [output / 'model.h'] + ([output / 'run'] if runner else [])}
        save()
        print(output / ('run' if runner else 'model.h'))
        return 0
    except (OSError, ValueError, RuntimeError) as error:
        manifest['status'], manifest['error'] = 'failed', str(error)
        if output.is_dir():
            (output / 'manifest.json').write_text(json.dumps(manifest, indent=2) + '\n')
        print(f'cpphdl native graph: {error}', file=sys.stderr)
        return 1


if __name__ == '__main__':
    sys.exit(main())
