"""Bind the actual receive-gain IR to reviewed IHP HV device dimensions."""
from collections import Counter
import csv
import io
import math
import re

# Polarity, compact-model beta, physical total width in micrometers, count.
BINDINGS = (('N', .03, 100, 3), ('N', .06, 200, 3), ('N', .2, 800, 1),
            ('P', .03, 300, 2), ('P', .06, 600, 2), ('P', .02, 200, 9))
NUMBER = r'([-+0-9.eE]+)'
NODE = r'(n\d+)'


def process_deck(model, original, inventory, models, corner='mos_tt', temperature=27, *, profile='single_ended'):
    if corner not in ('mos_tt', 'mos_ss', 'mos_ff', 'mos_sf', 'mos_fs') or not -40 <= temperature <= 125:
        raise ValueError('unsupported receive gain process conditions')
    records = list(csv.DictReader(io.StringIO(inventory)))
    devices = {}
    for row in records:
        key = int(row['id'])
        if key in devices or not all(re.fullmatch(r'0|n[1-9]\d*', row[name]) for name in ('a', 'b', 'gate', 'body')):
            raise ValueError('invalid receive gain device identity or terminal')
        if row['kind'] not in ('R', 'C', 'N', 'P'):
            raise ValueError('unknown receive gain physical law')
        for field in ('value', 'initial', 'width_um', 'length_um'):
            row[field] = float(row[field])
            if not math.isfinite(row[field]):
                raise ValueError('nonfinite receive gain physical parameter')
        devices[key] = row
    if profile not in ('single_ended', 'differential', 'reference'):
        raise ValueError('unknown receive analog component profile')
    bindings = BINDINGS
    if profile == 'differential':
        bindings = (('N', .003, 10, 5), ('N', .03, 100, 13), ('N', .06, 200, 3),
                    ('N', .2, 800, 1), ('P', .003, 30, 5), ('P', .03, 300, 2),
                    ('P', .06, 600, 2), ('P', .02, 200, 4))
    if profile == 'reference':
        bindings = tuple(('N', .4, 1600, 1) if entry[:2] == ('N', .2) else entry
                         for entry in BINDINGS if entry[:2] != ('P', .02))
    counts = {'single_ended': Counter(R=24, C=24, N=7, P=13),
              'differential': Counter(R=34, C=30, N=22, P=13),
              'reference': Counter(R=10, C=21, N=7, P=4)}[profile]
    if Counter(row['kind'] for row in records) != counts:
        raise ValueError('changed receive gain passive/MOS inventory')
    expected = Counter({(kind, width): count for kind, _, width, count in bindings})
    if Counter((row['kind'], row['width_um']) for row in records if row['kind'] in ('N', 'P')) != expected:
        raise ValueError('changed receive gain physical geometry inventory')
    marker, suffix = '    analog begin\n', '    end\nendmodule\n'
    if model.count(marker) != 1 or not model.endswith(suffix):
        raise ValueError('changed receive gain analog contribution block')
    resistor = re.compile(r'I\(' + NODE + ', ' + NODE + r'\) <\+ V\(' + NODE + ', ' + NODE + r'\) / ' + NUMBER + ';')
    channel = re.compile(r'I\(' + NODE + ', ' + NODE + r'\) <\+ cpphdl_channel\(V\(' + NODE + ', ' + NODE + r'\), V\('
                         + NODE + ', ' + NODE + r'\), ' + NUMBER + ', ' + NUMBER + ', ' + NUMBER + r'\);')
    node = lambda value: '0' if value == 'n0' else value
    identifier, seen = None, set()
    for line in model.split(marker, 1)[1].removesuffix(suffix).splitlines():
        line = line.strip()
        annotation = re.match(r'// device (\d+), ', line)
        if annotation:
            identifier = int(annotation[1])
            continue
        if not line or line.startswith('//'):
            continue
        if identifier is None or identifier not in devices or identifier in seen:
            raise ValueError('unidentified or duplicate receive gain law')
        row = devices[identifier]
        seen.add(identifier)
        identifier = None
        match = resistor.fullmatch(line)
        if match:
            a, b, va, vb = map(node, match.groups()[:4])
            value = float(match[5])
            if (row['kind'] != 'R' or (a, b) != (va, vb) or (a, b) != (row['a'], row['b'])
                    or value <= 0 or not math.isclose(value, row['value'], rel_tol=1e-12)):
                raise ValueError('receive gain resistor differs from actual IR')
            continue
        match = channel.fullmatch(line)
        if not match:
            raise ValueError('unbound receive gain contribution')
        a, b, gate, source, drain, drain_source = map(node, match.groups()[:6])
        beta, threshold, sign = map(float, match.groups()[6:])
        choices = [entry for entry in bindings if entry[0] == row['kind'] and math.isclose(entry[1], beta, rel_tol=1e-12)]
        if (row['kind'] not in ('N', 'P') or len(choices) != 1 or (a, b) != (row['a'], row['b'])
                or gate != row['gate'] or (source, drain, drain_source) != (b, a, b)
                or not math.isclose(threshold, .7) or sign != (1 if row['kind'] == 'N' else -1)
                or row['length_um'] != .45 or row['width_um'] != choices[0][2]):
            raise ValueError('receive gain MOS differs from actual IR or reviewed geometry')
    if seen != {key for key, row in devices.items() if row['kind'] != 'C'}:
        raise ValueError('missing receive gain current contributions')
    observations = dict(re.findall(r'^let observation(\d+) = v\((n\d+)\)$', original, re.M))
    if set(observations) != set(map(str, range(8))):
        raise ValueError('changed receive gain physical interface')
    vdd = observations['1' if profile == 'reference' else '5']
    caps = {}
    for line in original.splitlines():
        match = re.fullmatch(r'Cdevice(\d+) (0|n\d+) (0|n\d+) ' + NUMBER + ' IC=' + NUMBER, line)
        if match:
            key = int(match[1])
            if key in caps:
                raise ValueError('duplicate receive gain storage')
            caps[key] = (match[2], match[3], float(match[4]), float(match[5]))
    expected_caps = {key: (row['a'], row['b'], row['value'], row['initial'])
                     for key, row in devices.items() if row['kind'] == 'C'}
    if caps != expected_caps:
        raise ValueError('receive gain storage differs from actual IR')
    physical = []
    for key, row in devices.items():
        kind = row['kind']
        if kind == 'C':
            if row['value'] <= 0:
                raise ValueError('nonpositive receive gain storage')
            continue
        if kind == 'R':
            physical.append(f'Rphysical{key} {row["a"]} {row["b"]} {row["value"]:.17g}')
        else:
            if row['body'] != ('0' if kind == 'N' else vdd):
                raise ValueError('receive gain body is outside its physical supply domain')
            model_name = 'sg13_hv_nmos' if kind == 'N' else 'sg13_hv_pmos'
            physical.append(f'Xphysical{key} {row["a"]} {row["gate"]} {row["b"]} {row["body"]} '
                            f'{model_name} w={row["width_um"]:g}u l={row["length_um"]:g}u')
    lines = original.splitlines()
    if lines.count('.control') != 1 or sum(line.startswith('Ndesign ') for line in lines) != 1:
        raise ValueError('changed receive gain process experiment')
    deck = [lines[0], f'.lib "{models / "cornerMOShv.lib"}" {corner}', f'.temp {temperature:g}']
    for line in lines[1:]:
        if line.startswith(('.model CircuitModel ', 'Ndesign ', 'pre_osdi ')):
            continue
        if line == '.control':
            deck += physical
        deck.append(line)
    result = '\n'.join(deck) + '\n'
    if any(token in result for token in ('CircuitModel', 'cpphdl_channel', 'design.osdi')):
        raise ValueError('generic channel remains in receive gain process experiment')
    return result, dict(component_profile=profile, mos_count=counts['N'] + counts['P'], resistor_count=counts['R'], capacitor_count=counts['C'],
                        scope='explicit total widths and reviewed connectivity; valid fingers selected separately')
