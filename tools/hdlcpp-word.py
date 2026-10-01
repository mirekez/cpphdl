#!/usr/bin/env python3
"""Opt-in two-state word-model frontend for hdlcpp (sv2v/Yosys assisted)."""

import argparse
import hashlib
import json
from pathlib import Path
import re
import shutil
import subprocess
import sys


def normalize_literals(source):
    pattern = re.compile(r"(\d+)'h([0-9a-fA-F_]+)\[(\d+)(-:|\+:)(\d+)\]")

    def fold(match):
        bits, digits, first, direction, width = match.groups()
        bits, first, width = int(bits), int(first), int(width)
        if direction == '-:':
            first -= width - 1
        if width <= 0 or first < 0 or first + width > bits:
            raise ValueError('out-of-range constant selection emitted by sv2v')
        value = (int(digits.replace('_', ''), 16) >> first) & ((1 << width) - 1)
        return f"{width}'h{value:x}"

    pieces = re.split(r'("(?:\\.|[^"\\])*"|//[^\n]*|/\*[\s\S]*?\*/)', source)
    for index in range(0, len(pieces), 2):
        pieces[index] = pattern.sub(fold, pieces[index])
    return ''.join(pieces)


def scalar_aliases(module):
    result = []
    for name, net in module['netnames'].items():
        if name in module['ports'] or not net['hide_name'] or net['attributes']:
            continue
        bits = net['bits']
        signals = {bit for bit in bits if isinstance(bit, int)}
        if len(signals) == 1 and any(isinstance(bit, str) for bit in bits):
            if re.fullmatch(r'[A-Za-z0-9_.$:\[\]]+', name):
                result.append(name)
    return sorted(result)


def main(arguments=None):
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--top', required=True)
    parser.add_argument('--output', type=Path, required=True)
    parser.add_argument('--sv2v', default='sv2v')
    parser.add_argument('--yosys', default='yosys')
    parser.add_argument('-I', dest='includes', action='append', default=[])
    parser.add_argument('-D', dest='defines', action='append', default=[])
    parser.add_argument('sources', type=Path, nargs='+')
    args = parser.parse_args(arguments)
    if not re.fullmatch(r'[A-Za-z_][A-Za-z0-9_]*', args.top):
        parser.error('top must be a simple identifier')
    output = args.output.resolve()
    if output.exists() and (not output.is_dir() or any(output.iterdir())):
        parser.error('output must be a new or empty directory')
    manifest = {'frontend': 'hdlcpp-word', 'representation': 'cxxrtl-c++',
                'semantics': 'synthesized two-state', 'top': args.top,
                'status': 'building', 'commands': []}
    try:
        sources = [path.resolve(strict=True) for path in args.sources]
        includes = [str(Path(path).resolve(strict=True)) for path in args.includes]
        binaries = {}
        for name in ('sv2v', 'yosys'):
            binaries[name] = shutil.which(getattr(args, name))
            if not binaries[name]:
                raise RuntimeError(f'required tool not found: {getattr(args, name)}')
        output.mkdir(parents=True, exist_ok=True)

        def save():
            (output / 'manifest.json').write_text(json.dumps(manifest, indent=2) + '\n')

        def run(command, label, destination=None):
            manifest['commands'].append(command)
            save()
            with (output / (label + '.log')).open('w') as log:
                result = subprocess.run(command, cwd=output, stdout=destination or log, stderr=log)
            if result.returncode:
                raise RuntimeError(f'{label} failed ({result.returncode}); see {output / (label + ".log")}')

        manifest['sources'] = {str(path): hashlib.sha256(path.read_bytes()).hexdigest() for path in sources}
        manifest['include_directories'] = includes
        manifest['defines'] = args.defines
        with (output / 'design.v').open('w') as stream:
            run([binaries['sv2v'], '--top=' + args.top,
                 *['-I' + path for path in includes], *['-D' + value for value in args.defines],
                 *map(str, sources)], 'sv2v', stream)
        normalized = normalize_literals((output / 'design.v').read_text())
        (output / 'normalized.v').write_text(normalized)
        script = '\n'.join([
            # Process lowering preserves ordered writes before scheduling.
            # Driver boundaries avoid making unrelated packed fields depend
            # on one another; native C++ remains the second stage's input.
            'read_verilog normalized.v', 'hierarchy -check -top ' + args.top,
            'proc', 'flatten', 'opt -full', 'memory -nomap', 'opt -full',
            'splitnets -driver', 'opt_clean', 'check -assert',
            'write_rtlil model.il', 'write_json model.json', '',
        ])
        (output / 'lower.ys').write_text(script)
        run([binaries['yosys'], '-Q', '-T', '-s', 'lower.ys'], 'yosys')
        module = json.loads((output / 'model.json').read_text())['modules'][args.top]
        aliases = scalar_aliases(module)
        manifest['split_scalar_aliases'] = aliases
        script = ['read_rtlil model.il']
        if aliases:
            # A constant/replicated-bit mask is wiring, not a stateful producer.
            # Separate its bits so aliases cannot create artificial feedback.
            selection = ' '.join('w:' + name for name in aliases)
            script += ['select -assert-count ' + str(len(aliases)) + ' ' + selection,
                       'splitnets ' + selection, 'select *', 'opt_clean']
        script += ['check -assert', 'write_rtlil model.il', 'write_json model.json',
                   'write_cxxrtl -header -O6 -g0 model.cc', '']
        (output / 'emit.ys').write_text('\n'.join(script))
        run([binaries['yosys'], '-Q', '-T', '-s', 'emit.ys'], 'emit')
        manifest['artifacts'] = {}
        for name in ('design.v', 'normalized.v', 'model.il', 'model.json', 'model.h', 'model.cc'):
            manifest['artifacts'][name] = hashlib.sha256((output / name).read_bytes()).hexdigest()
        manifest['status'] = 'complete'
        save()
        print(output / 'model.cc')
        return 0
    except (OSError, RuntimeError, ValueError) as error:
        manifest['status'] = 'failed'
        manifest['error'] = str(error)
        if output.is_dir():
            (output / 'manifest.json').write_text(json.dumps(manifest, indent=2) + '\n')
        print(f'hdlcpp-word: {error}', file=sys.stderr)
        return 1


if __name__ == '__main__':
    sys.exit(main())
