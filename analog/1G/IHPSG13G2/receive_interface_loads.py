#!/usr/bin/env python3
"""Extract comparator-return loads from the routed lane's OpenRCX SPEF."""
import argparse
import hashlib
import json
import math
from pathlib import Path
import re


def digest(path):
    return hashlib.sha256(Path(path).read_bytes()).hexdigest()


def positive(value):
    result = float(value)
    if not math.isfinite(result) or result < 0:
        raise ValueError('invalid capacitance: ' + value)
    return result


def group(text, kind, name):
    """Read one named Liberty group, keeping nested timing tables inside it."""
    pattern = r'\b' + kind + r'\s*\(\s*"?' + re.escape(name) + r'"?\s*\)\s*\{'
    matches = list(re.finditer(pattern, text))
    if len(matches) != 1:
        raise ValueError('missing or ambiguous Liberty group: ' + kind + ' ' + name)
    start = matches[0].end()
    depth, quoted, escaped = 1, False, False
    for index in range(start, len(text)):
        char = text[index]
        if escaped:
            escaped = False
        elif char == '\\':
            escaped = True
        elif char == '"':
            quoted = not quoted
        elif not quoted:
            depth += (char == '{') - (char == '}')
            if depth == 0:
                return text[start:index]
    raise ValueError('unterminated Liberty group')


def pin_capacitance(text, cell, pin):
    text = re.sub(r'/\*.*?\*/|//[^\n]*', '', text, flags=re.S)
    if len(re.findall(r'capacitive_load_unit\s*\(\s*1\s*,\s*pf\s*\)', text)) != 1:
        raise ValueError('expected Liberty capacitance unit 1 pf')
    body = group(group(text, 'cell', cell), 'pin', pin)
    if re.findall(r'\bdirection\s*:\s*"?(\w+)"?\s*;', body) != ['input']:
        raise ValueError('expected an input load pin')
    values = re.findall(r'\bcapacitance\s*:\s*([^;]+);', body)
    if len(values) != 1:
        raise ValueError('missing or ambiguous input pin capacitance')
    nominal = positive(values[0])
    values = re.findall(r'\b(?:rise|fall)_capacitance\s*:\s*([^;]+);', body)
    for pair in re.findall(r'\b(?:rise|fall)_capacitance_range\s*\(([^)]+)\)', body):
        if len(pair.split(',')) != 2:
            raise ValueError('invalid pin capacitance range')
        values.extend(pair.split(','))
    return dict(capacitance_pf=nominal,
        maximum_characterized_pin_capacitance_pf=max([nominal] + [positive(v) for v in values]))


def extract(spef, libraries, count=13):
    """Accept the pinned OpenRCX format; reject pin-inclusive or ambiguous data."""
    if not re.search(r'^\*C_UNIT 1 PF$', spef, re.M) or '"PIN_CAP NONE"' not in spef:
        raise ValueError('expected PF wire-only SPEF; pin capacitance must not be counted twice')
    if not re.search(r'^\*DELIMITER :$', spef, re.M):
        raise ValueError('unsupported SPEF pin delimiter')
    mapping = re.findall(r'^\*(\d+) (\S+)$', spef, re.M)
    names = dict(mapping)
    if len(names) != len(mapping) or len(set(names.values())) != len(names):
        raise ValueError('ambiguous SPEF name map')
    sections = re.findall(r'^\*D_NET \*(\d+) ([^\n]+)\n(.*?)^\*END$', spef, re.M | re.S)
    if len({number for number, _, _ in sections}) != len(sections):
        raise ValueError('duplicate extracted net')
    records, cache = [], {}
    for number, total, body in sections:
        name = names.get(number, '')
        if not re.fullmatch(r'comparisons_in\[\d+\]', name):
            continue
        index = int(re.search(r'\d+', name)[0])
        if index >= count:
            raise ValueError('unexpected connected compatibility input: ' + name)
        parts = re.fullmatch(r'\*CONN\n(.*?)\*CAP\n(.*?)\*RES\n(.*)', body, re.S)
        if not parts:
            raise ValueError('unsupported extracted net sections')
        ports, loads = [], []
        for line in parts[1].splitlines():
            port = re.fullmatch(r'\*P (\S+) I', line)
            load = re.fullmatch(r'\*I \*(\d+):([^ ]+) I \*D (\S+)', line)
            if port:
                ports.append(names.get(port[1][1:], '') if port[1].startswith('*') else port[1])
            elif load and load[1] in names:
                loads.append(dict(instance=names[load[1]], cell=load[3], pin=load[2]))
            else:
                raise ValueError('unsupported or non-input return connection: ' + line)
        if ports != [name] or not loads or len({(p['instance'], p['pin']) for p in loads}) != len(loads):
            raise ValueError('incomplete or duplicate return load')
        ground, coupling, ids = 0.0, 0.0, set()
        for line in parts[2].splitlines():
            fields = line.split()
            if len(fields) not in (3, 4) or fields[0] in ids:
                raise ValueError('invalid or duplicate extracted capacitance')
            ids.add(fields[0])
            value = positive(fields[-1])
            if len(fields) == 3:
                ground += value
            else:
                coupling += value
        wire = positive(total)
        if not ids or not math.isclose(ground + coupling, wire, rel_tol=2e-5, abs_tol=1e-10):
            raise ValueError('extracted capacitance sum disagrees with D_NET')
        corners = {}
        for corner, contents in libraries.items():
            caps = []
            for load in loads:
                key = (corner, load['cell'], load['pin'])
                if key not in cache:
                    cache[key] = pin_capacitance(contents, load['cell'], load['pin'])
                caps.append(cache[key])
            pin = sum(c['capacitance_pf'] for c in caps)
            maximum = sum(c['maximum_characterized_pin_capacitance_pf'] for c in caps)
            corners[corner] = dict(pin_capacitance_pf=pin,
                maximum_characterized_pin_capacitance_pf=maximum,
                grounded_aggressor_total_pf=ground + coupling + pin,
                grounded_aggressor_characterized_total_pf=ground + coupling + maximum,
                opposite_equal_swing_characterized_total_pf=ground + 2 * coupling + maximum)
        records.append(dict(port=name, grounded_wire_pf=ground, coupled_wire_pf=coupling,
            reported_wire_pf=wire, pins=loads, corners=corners))
    if {r['port'] for r in records} != {f'comparisons_in[{i}]' for i in range(count)}:
        raise ValueError('missing comparator-return extraction')
    return sorted(records, key=lambda r: int(re.search(r'\d+', r['port'])[0]))


def run(args):
    implementation = args.implementation.resolve()
    proof_path = implementation / 'physical_module.json'
    proof = json.loads(proof_path.read_text())
    if proof.get('status') not in ('failed', 'component_checks_pass') or proof.get('top') != 'ADCReceiveLaneController':
        raise ValueError('completed routed receive-lane receipt required')
    spef = implementation / 'core.spef'
    if proof.get('artifact_sha256', {}).get(spef.name) != digest(spef):
        raise ValueError('changed routed extraction')
    libraries = {}
    hashes = {str(p): digest(p) for p in (Path(__file__).resolve(), proof_path, spef)}
    for suffix in ('slow_1p08V_125C', 'fast_1p32V_m40C'):
        path = args.pdk.resolve() / 'ihp-sg13g2/libs.ref/sg13g2_stdcell/lib' / f'sg13g2_stdcell_{suffix}.lib'
        if proof.get('input_sha256', {}).get(str(path)) != digest(path):
            raise ValueError('changed implementation library: ' + suffix)
        hashes[str(path)] = digest(path)
        libraries[suffix] = path.read_text()
    records = extract(spef.read_text(), libraries)
    if any(digest(p) != sha for p, sha in hashes.items()):
        raise ValueError('load evidence changed during extraction')
    result = dict(status='reported', implementation_status=proof['status'],
        ports=records, input_sha256=hashes,
        scope='nominal routed return RC plus Liberty receiver pins; excludes external analog routing and fill coupling; 2x coupling assumes opposite equal-voltage transitions and is not a noise or transient qualification')
    args.output.parent.mkdir(parents=True, exist_ok=True)
    with args.output.open('x') as output:
        output.write(json.dumps(result, indent=2) + '\n')
    print('Reported comparator-return loads:', len(records))


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    for name in ('implementation', 'pdk', 'output'):
        parser.add_argument('--' + name, type=Path, required=True)
    run(parser.parse_args())
