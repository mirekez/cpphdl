"""Prove same-function sizing plus insertion of non-inverting repair buffers."""
import hashlib
from pathlib import Path
import re

from buffer_equivalence import BUFFERS, connectivity, parse
from sizing_equivalence import models


def compare(before_path, after_path, model_path):
    before, after = parse(before_path), parse(after_path)
    if before['module'] != after['module'] or before['interface'] != after['interface']:
        raise ValueError('repair changed the top-level interface')
    old, new = before['cells'], after['cells']
    if set(old) - set(new):
        raise ValueError('repair removed existing instances')
    added = set(new) - set(old)
    library = models(model_path)
    transparent = library.get('sg13g2_buf_1')
    if transparent != '(A,X);inputA;outputX;assignX=A;':
        raise ValueError('reference buffer must explicitly implement X = A')
    for name in added:
        master, pins = new[name]
        if master not in BUFFERS or set(pins) != {'A', 'X'} or library.get(master) != transparent:
            raise ValueError('repair inserted something other than a proven non-inverting buffer: ' + name)
    replacements = []
    for name, (master, pins) in old.items():
        target, target_pins = new[name]
        if set(pins) != set(target_pins):
            raise ValueError('repair changed a surviving cell interface: ' + name)
        if master != target:
            family = [re.fullmatch(r'(sg13g2_\w+)_(1|2|4|8|16)', m) for m in (master, target)]
            same_family = all(family) and family[0][1] == family[1][1]
            # IHP delay cells implement X = A as well. Their replacement can
            # change hold timing, which must still pass fresh extracted STA.
            same_identity = (master in BUFFERS and target in BUFFERS
                             and library.get(master) == transparent == library.get(target))
            if (not (same_family or same_identity) or master not in library
                    or library.get(target) != library[master]):
                raise ValueError('repair changed a cell logic function: ' + name)
            replacements.append(dict(instance=name, before=master, after=target))
            new[name] = (master, target_pins)
    if connectivity(before, set()) != connectivity(after, added):
        raise ValueError('repair changed endpoint connectivity')
    return dict(status='pass', top=before['module'], inserted_buffers=sorted(added),
                replacements=replacements,
                input_sha256={str(Path(p).resolve()): hashlib.sha256(Path(p).read_bytes()).hexdigest()
                              for p in (before_path, after_path, model_path)},
                scope='identical drive-variant logic and endpoint connectivity after contracting new buffers',
                timing_qualified=False, physical_qualified=False, full_chip_qualified=False)
