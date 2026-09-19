#!/usr/bin/env python3
"""Experimental word-level simulation backend, independent of the comb scheduler."""

import argparse
import hashlib
import json
from pathlib import Path
import re
import shutil
import subprocess
import sys


def executable(name):
    found = shutil.which(name)
    if not found:
        raise RuntimeError(f"Required executable not found: {name}")
    return str(Path(found).resolve())


def ordered_sources(directory):
    files = sorted(directory.glob('*.sv'))
    packages = {}
    for path in files:
        for name in re.findall(r'\bpackage\s+(\w+)\s*;', path.read_text()):
            if name in packages:
                raise RuntimeError(f"Duplicate generated package: {name}")
            packages[name] = path
    result, visiting, complete = [], set(), set()

    def visit(path):
        if path in complete:
            return
        if path in visiting:
            raise RuntimeError(f"Cyclic generated package dependency: {path}")
        visiting.add(path)
        for name in sorted(set(re.findall(r'\b(\w+)\s*::', path.read_text()))):
            dependency = packages.get(name)
            if dependency is not None and dependency != path:
                visit(dependency)
        visiting.remove(path)
        complete.add(path)
        result.append(path)

    for path in files:
        visit(path)
    if not result:
        raise RuntimeError('CppHDL produced no SystemVerilog')
    return result


def main(arguments=None):
    arguments = list(sys.argv[1:] if arguments is None else arguments)
    compiler_arguments = []
    if '--' in arguments:
        split = arguments.index('--')
        arguments, compiler_arguments = arguments[:split], arguments[split + 1:]
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--cpphdl', required=True)
    parser.add_argument('--sv2v', default='sv2v')
    parser.add_argument('--yosys', default='yosys')
    parser.add_argument('--top', required=True)
    parser.add_argument('--output', required=True, type=Path)
    parser.add_argument('sources', nargs='+', type=Path)
    args = parser.parse_args(arguments)
    if not re.fullmatch(r'[A-Za-z_][A-Za-z0-9_]*', args.top):
        parser.error('top must be a simple module identifier')
    if args.output.exists() and (not args.output.is_dir() or any(args.output.iterdir())):
        parser.error('output directory must be new or empty (no stale models)')
    try:
        cpphdl, sv2v, yosys = map(executable, [args.cpphdl, args.sv2v, args.yosys])
        sources = [path.resolve(strict=True) for path in args.sources]
        output = args.output.resolve()
        output.mkdir(parents=True, exist_ok=True)
        rtl = output / 'rtl'
        rtl.mkdir()
        manifest = {'backend': 'yosys-cxxrtl', 'source_kind': 'cpphdl-c++',
                    'top': args.top, 'status': 'building', 'commands': [],
                    'sources': {str(path): hashlib.sha256(path.read_bytes()).hexdigest() for path in sources}}

        def save():
            (output / 'manifest.json').write_text(json.dumps(manifest, indent=2) + '\n')

        def run(command, label, stdout=None):
            manifest['commands'].append(command)
            save()
            with (output / f'{label}.log').open('w') as log:
                result = subprocess.run(command, cwd=output, stdout=stdout or log, stderr=log)
            if result.returncode:
                raise RuntimeError(f"{label} failed ({result.returncode}); see {output / (label + '.log')}")

        try:
            run([cpphdl, '--generated-dir=' + str(rtl), *map(str, sources), '--', *compiler_arguments], 'cpphdl')
            # Some legacy converter assertions log an error without a nonzero
            # exit code. Never accept their partial RTL as a runnable model.
            if 'ASSERT at ' in (output / 'cpphdl.log').read_text():
                raise RuntimeError(f"CppHDL assertion; see {output / 'cpphdl.log'}")
            generated = ordered_sources(rtl)
            manifest['generated_rtl'] = {str(path.relative_to(output)): hashlib.sha256(path.read_bytes()).hexdigest()
                                         for path in generated}
            with (output / 'design.v').open('w') as flattened:
                run([sv2v, '--top=' + args.top, *map(str, generated)], 'sv2v', flattened)
            # RTLIL is the execution IR: structural flattening, constant folding
            # and word-level optimization precede C++ emission. No bit blasting,
            # proxy objects, source-method caching, or old comb scheduler is used.
            script = '\n'.join([
                'read_verilog design.v',
                'hierarchy -check -top ' + args.top,
                'proc', 'flatten', 'opt -full', 'memory -nomap', 'opt -full',
                # Independent fields must not share a scheduling dependency
                # merely because the source stored them in one packed object.
                'splitnets -driver', 'opt_clean',
                'check -assert', 'write_rtlil model.il', 'write_json model.json',
                'write_cxxrtl -header -O6 -g0 model.cc', '',
            ])
            (output / 'lower.ys').write_text(script)
            run([yosys, '-Q', '-T', '-s', 'lower.ys'], 'yosys')
            for name in ['model.il', 'model.json', 'model.h', 'model.cc']:
                if not (output / name).is_file():
                    raise RuntimeError(f"Backend did not produce {name}")
            manifest['artifacts'] = {name: hashlib.sha256((output / name).read_bytes()).hexdigest()
                                     for name in ['design.v', 'model.il', 'model.json', 'model.h', 'model.cc']}
            manifest['status'] = 'complete'
            save()
        except Exception as error:
            manifest['status'] = 'failed'
            manifest['error'] = str(error)
            save()
            raise
        print(output / 'model.cc')
        return 0
    except (OSError, RuntimeError) as error:
        print(f'cpphdl-netlist: {error}', file=sys.stderr)
        return 1


if __name__ == '__main__':
    sys.exit(main())
