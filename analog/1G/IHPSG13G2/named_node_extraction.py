"""Retain internal node identities when extracting compact transistor macros.

Magic drops anonymous-node coupling during resistance splitting. Naming those
same conductor points first retains it. Device connectivity and the complete
capacitance matrix must remain unchanged by this label-only operation.
"""
from collections import Counter, defaultdict
import json
import math
import os
from pathlib import Path
import re

from extract_analog import digest, extract, inventory
from extraction_capacitance import conserve
from tool_process import execute


def label_plan(ext):
    scales = re.findall(r'^scale (\S+) (\S+) (\S+)$', ext, re.M)
    if len(scales) != 1 or not math.isclose(float(scales[0][1]), 1):
        raise ValueError('unknown extraction units')
    # EXT length units are centimicrons, independently of Magic's box units.
    unit_um = float(scales[0][2]) * .01
    if not math.isfinite(unit_um) or unit_um <= 0:
        raise ValueError('invalid extraction length scale')
    if 'RC_INTERNAL_' in ext:
        raise ValueError('internal extraction label collision')
    nodes = re.findall(r'^node "([^"\n]+)" \S+ \S+ (-?\d+) (-?\d+) (\S+)', ext, re.M)
    bodies = {}
    for line in ext.splitlines():
        if not line.startswith('device rsubckt '):
            continue
        fields = line.split()
        terminals = re.findall(r'"([^"\n]+)"', line)
        if fields[2] not in ('rppd', 'rhigh') or len(terminals) != 4:
            raise ValueError('unreviewed resistor-body anchor')
        body = terminals[1]
        if body in bodies:
            raise ValueError('duplicate resistor-body anchor')
        bodies[body] = tuple(map(int, fields[3:5]))
    aliases, commands, seen = {}, [], set()
    for name, x, y, layer in nodes:
        if not name.startswith('a_') or not name.endswith('#'):
            continue
        if name in seen:
            raise ValueError('duplicate anonymous conductor')
        seen.add(name)
        if layer == 'pres' and name in bodies:
            if (int(x), int(y)) != bodies[name]:
                raise ValueError('resistor body and conductor anchor disagree')
            # Retain the identity used by the R3 body-partition adapter. It
            # owns this node's charge and finite-resistance midpoint; treating
            # it as an ordinary wire would lose that device association.
            continue
        if layer not in ('p', 'ndif', 'pdif'):
            raise ValueError('unreviewed anonymous conductor or duplicate identity')
        alias = 'RC_INTERNAL_' + str(len(aliases))
        aliases[name] = alias
        x, y = int(x), int(y)
        coordinates = ' '.join(f'{v * unit_um:.12g}um' for v in (x, y, x + 1, y + 1))
        commands += ['box values ' + coordinates, f'label {alias} center {layer}']
    return aliases, commands


def check_relabel(original, named, ports, aliases):
    before, after = inventory(original, ports), inventory(named, ports)

    def devices(text, rename):
        result = []
        for line in text.splitlines():
            fields = line.split(';')[0].split()
            if fields and fields[0].startswith('X'):
                result.append(tuple(rename.get(n, n) for n in fields[1:6]) + tuple(sorted(fields[6:])))
        return Counter(result)

    def matrix(capacitors, rename):
        result = defaultdict(float)
        for a, b, value in capacitors:
            result[tuple(sorted((rename.get(a, a), rename.get(b, b))))] += value
        return result

    if len(set(aliases.values())) != len(aliases) or devices(original, aliases) != devices(named, {}):
        raise ValueError('internal labelling changed transistor topology or geometry')
    a, b = matrix(before['capacitors'], aliases), matrix(after['capacitors'], {})
    if set(a) != set(b) or any(not math.isclose(a[k], b[k], rel_tol=1e-9, abs_tol=1e-22) for k in a):
        raise ValueError('internal labelling changed the capacitance matrix')
    if before['resistors'] or after['resistors']:
        raise ValueError('label comparison requires unsplit capacitance extraction')
    return dict(renamed_nodes=len(aliases), unchanged_device_count=sum(before['models'].values()),
                unchanged_capacitance_entries=len(a))


def normalize_zero_ground(reference, distributed, text, substrate):
    """Remove only measured duplicated coupling where original ground C is zero.

    The shared conservation checker already handles nonzero ground C. Keep it
    unchanged for existing long-running jobs and handle this boundary separately.
    """
    parent = {}

    def root(node):
        parent.setdefault(node, node)
        if parent[node] != node:
            parent[node] = root(parent[node])
        return parent[node]

    for a, b, _ in distributed['resistors']:
        parent[root(a)] = root(b)
    names = defaultdict(set)
    for a, b, _ in reference['capacitors']:
        for node in (a, b):
            names[root(node)].add(node)
    if any(len(values) != 1 for values in names.values()):
        raise ValueError('distributed extraction shorts original nets')
    logical = {key: next(iter(values)) for key, values in names.items()}

    def name(node):
        if root(node) not in logical:
            raise ValueError('unmapped capacitance endpoint after internal labelling')
        return logical[root(node)]

    def matrix(capacitors):
        result = defaultdict(float)
        for a, b, value in capacitors:
            a, b = name(a), name(b)
            if a != b:
                result[tuple(sorted((a, b)))] += value
        return result

    wanted, observed = matrix(reference['capacitors']), matrix(distributed['capacitors'])
    adjustments = {}
    for pair, actual in observed.items():
        if substrate not in pair or wanted.get(pair, 0) != 0 or actual == 0:
            continue
        node = next(n for n in pair if n != substrate)
        coupling = sum(v for k, v in wanted.items() if node in k and substrate not in k)
        if not math.isclose(actual, coupling, rel_tol=1e-4, abs_tol=1e-20):
            raise ValueError('unexplained added capacitance on zero-ground node')
        adjustments[node] = dict(reference_ground_F=0, raw_ground_F=actual, coupling_F=coupling,
                                 reason='Magic #550 coupling duplicated onto zero original substrate capacitance')
    lines = []
    for line in text.splitlines():
        fields = line.split(';')[0].split()
        if fields and fields[0].startswith('C'):
            a, b = name(fields[1]), name(fields[2])
            if a != b and ((a == substrate and b in adjustments) or (b == substrate and a in adjustments)):
                line = ' '.join(fields[:3]) + ' 0'
        lines.append(line)
    return '\n'.join(lines) + '\n', adjustments


def extract_named(gds, top, ports, pdk, magic, work, substrate):
    work = Path(work).resolve()
    work.mkdir(parents=True, exist_ok=True)
    if any(work.iterdir()):
        raise ValueError('named extraction needs a fresh directory')
    raw = work / 'raw'
    try:
        extract(gds, top, ports, pdk, magic, raw, substrate=substrate)
    except ValueError as error:
        if not str(error).startswith(('unmapped extracted capacitor endpoint:', 'unexplained substrate capacitance change:')):
            raise
    original = json.loads((raw / 'extraction.json').read_text())
    hashes = dict(original['source_sha256'])
    hashes[str(Path(__file__).resolve())] = digest(__file__)
    report = dict(status='running', work=str(work), source_sha256=hashes,
                  scope='named conductor anchors with unchanged devices/capacitance matrix; field calibration remains separate')
    try:
        aliases, commands = label_plan((raw / 'ExtractedAnalog.ext').read_text())
        text = (raw / 'extract.tcl').read_text()
        if text.count('extract all\n') != 1:
            raise ValueError('extraction script contract changed')
        named = work / 'named'
        named.mkdir()
        (named / 'extract.tcl').write_text(text.replace('extract all\n', '\n'.join(commands) + '\nextract all\n', 1))
        tech = Path(pdk).resolve() / 'ihp-sg13g2/libs.tech/magic'
        with (named / 'magic.log').open('w') as log:
            execute([str(Path(magic).resolve()), '-dnull', '-noconsole', '-rcfile', str(tech / 'ihp-sg13g2.magicrc'), 'extract.tcl'],
                cwd=named, env=dict(os.environ, OMP_NUM_THREADS='1'), stdout=log, stderr=-2,
                check=True, timeout=300, memory_exclusive=False)
        actual_ports = re.findall(r'^port "([^"]+)" (\d+)', (named / 'ExtractedAnalog.ext').read_text(), re.M)
        if [n for n, _ in sorted(actual_ports, key=lambda p: int(p[1]))] != ports:
            raise ValueError('internal labels changed external ports')
        reference = (named / 'capacitance.spice').read_text()
        distributed = (named / 'distributed.spice').read_text()
        report['labelling'] = check_relabel((raw / 'capacitance.spice').read_text(), reference, ports, aliases)
        report['aliases'] = aliases
        a, b = inventory(reference, ports), inventory(distributed, ports)
        normalized, report['zero_ground'] = normalize_zero_ground(a, b, distributed, substrate)
        (named / 'zero-ground-normalized.spice').write_text(normalized)
        corrected, report['conservation'] = conserve(a, inventory(normalized, ports), normalized, substrate)
        (work / 'conserved.spice').write_text(corrected)
        report['inventory'] = inventory(corrected, ports)
        if any(digest(p) != value for p, value in hashes.items()):
            raise ValueError('named extraction inputs changed')
        report.update(status='pass', artifact_sha256={str(p): digest(p) for p in work.rglob('*') if p.is_file()})
        return report
    except BaseException as error:
        report.update(status='fail', error=str(error))
        raise
    finally:
        (work / 'checks.json').write_text(json.dumps(report, indent=2) + '\n')
