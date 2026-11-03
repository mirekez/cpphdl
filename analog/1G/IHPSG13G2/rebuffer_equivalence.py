"""Check timing repair that replaces buffer trees and resizes surviving cells.

Only characterized identity buffers may disappear or appear. All remaining
cell functions and endpoint partitions must agree. Buffer direction and unique
drivers are checked before contraction; equivalence never qualifies timing.
"""
import argparse
import hashlib
import json
from pathlib import Path
import re

from buffer_equivalence import BUFFERS, connectivity, parse
from sizing_equivalence import models


def check_buffer_drivers(design, contracted, library):
    drivers = {}
    for net, direction in design['ports'].items():
        if direction in ('input', 'inout'):
            drivers.setdefault(net, []).append(('port', net))
    for name, (master, pins) in design['cells'].items():
        if master not in library:
            raise ValueError('missing cell model: ' + master)
        outputs = re.findall(r'(?:^|(?<=;))output([^;]+);', library[master])
        for declaration in outputs:
            for pin in declaration.split(','):
                # Clock-load cells may deliberately leave their output open.
                # Surviving pin sets are compared below; a new or removed
                # identity buffer must still expose both A and X.
                if pin in pins:
                    drivers.setdefault(pins[pin], []).append(('cell', name))
    for left in design['aliases']:
        drivers.setdefault(left, []).append(('alias', left))
    for bit in (0, 1):
        drivers.setdefault(('$constant', bit), []).append(('constant', bit))
    for name in contracted:
        pins = design['cells'][name][1]
        if drivers.get(pins['X']) != [('cell', name)]:
            raise ValueError('contracted buffer output has another driver: ' + name)
        visited = {name}
        net = pins['A']
        while True:
            source = drivers.get(net, [])
            if len(source) != 1:
                raise ValueError('contracted buffer input lacks a unique driver: ' + name)
            kind, upstream = source[0]
            if kind != 'cell' or upstream not in contracted:
                break
            if upstream in visited:
                raise ValueError('contracted buffers form a directed cycle')
            visited.add(upstream)
            net = design['cells'][upstream][1]['A']


def compare(before_path, after_path, model_path):
    before, after = parse(before_path), parse(after_path)
    if before['module'] != after['module'] or before['interface'] != after['interface']:
        raise ValueError('buffer repair changed the top-level interface')
    old, new = before['cells'], after['cells']
    removed, added = set(old) - set(new), set(new) - set(old)
    library = models(model_path)
    identity = '(A,X);inputA;outputX;assignX=A;'
    for design, names in ((before, removed), (after, added)):
        for name in names:
            master, pins = design['cells'][name]
            if master not in BUFFERS or set(pins) != {'A', 'X'} or library.get(master) != identity:
                raise ValueError('only proven identity buffers may be added or removed: ' + name)
        check_buffer_drivers(design, names, library)
    replacements = []
    for name in sorted(set(old) & set(new)):
        master, pins = old[name]
        target, target_pins = new[name]
        if set(pins) != set(target_pins):
            raise ValueError('buffer repair changed a surviving cell interface: ' + name)
        if master == target:
            continue
        variants = [re.fullmatch(r'(sg13g2_\w+)_(1|2|4|8|16)', m) for m in (master, target)]
        same_family = all(variants) and variants[0][1] == variants[1][1]
        if (not (same_family or master in BUFFERS and target in BUFFERS)
                or master not in library or library.get(target) != library[master]):
            raise ValueError('buffer repair changed a cell logic function: ' + name)
        replacements.append(dict(instance=name, before=master, after=target))
        new[name] = (master, target_pins)
    if connectivity(before, removed) != connectivity(after, added):
        raise ValueError('buffer repair changed endpoint connectivity')
    return dict(status='pass', top=before['module'], removed_buffers=sorted(removed),
                inserted_buffers=sorted(added), replacements=replacements,
                input_sha256={str(Path(p).resolve()): hashlib.sha256(Path(p).read_bytes()).hexdigest()
                              for p in (before_path, after_path, model_path)},
                scope='identical cell functions and endpoints after directed identity-buffer contraction',
                timing_qualified=False, physical_qualified=False, full_chip_qualified=False)


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    for name in ('before', 'after', 'models', 'output'):
        parser.add_argument('--' + name, type=Path, required=True)
    args = parser.parse_args()
    args.output.unlink(missing_ok=True)
    result = compare(args.before, args.after, args.models)
    result['source_sha256'] = {str(p.resolve()): hashlib.sha256(p.read_bytes()).hexdigest()
                              for p in (Path(__file__), Path(__file__).with_name('buffer_equivalence.py'),
                                        Path(__file__).with_name('sizing_equivalence.py'))}
    args.output.write_text(json.dumps(result, indent=2) + '\n')
