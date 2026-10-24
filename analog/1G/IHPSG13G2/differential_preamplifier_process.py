"""Bind the complete differential MDI preamplifier, preserving every passive law."""
from collections import Counter, defaultdict
import math
import re


def process_deck(model, original, models, corner='mos_tt', temperature=27):
    if corner not in ('mos_tt', 'mos_ss', 'mos_ff') or not math.isfinite(temperature) or not -40 <= temperature <= 125:
        raise ValueError('unsupported preamplifier process conditions')
    marker, suffix = '    analog begin\n', '    end\nendmodule\n'
    if model.count(marker) != 1 or not model.endswith(suffix):
        raise ValueError('changed preamplifier contribution block')
    lines = original.splitlines()
    observations = dict(re.findall(r'^let observation(\d+) = v\((n\d+)\)$', original, re.M))
    if set(observations) != set(map(str, range(12))) or lines.count('.control') != 1 or sum(x.startswith('Ndesign ') for x in lines) != 1:
        raise ValueError('changed preamplifier electrical interface')
    caps = re.findall(r'^Cdevice(\d+) (0|n\d+) (0|n\d+) (\S+) IC=(\S+)$', original, re.M)
    expected_caps = Counter({4e-14: 21, 2e-14: 9, 1e-11: 7, 1e-14: 6, 8e-14: 4,
        2.5e-13: 3, 1e-13: 2, 1.5e-13: 2, 2e-11: 1, 2e-12: 1, 3e-11: 1,
        5e-14: 1, 1e-12: 1, 7.5e-13: 1, 5e-13: 1, 3e-12: 1})
    if (len(caps) != 62 or len({x[0] for x in caps}) != len(caps) or
            Counter(float(x[3]) for x in caps) != expected_caps or any(float(x[4]) != 0 for x in caps)):
        raise ValueError('changed preamplifier storage or cold-start charge')
    cap_ids = {str(value) for value in re.findall(r'// Capacitor device (\d+) is stamped once by the SPICE host deck[.]', model)}
    if cap_ids != {x[0] for x in caps}:
        raise ValueError('preamplifier host storage differs from exported contributions')
    annotation = re.compile(r'// device (\d+), (?:IC ([^,]+), )?.*/([^/]+[.](?:hpp|cpp)):\d+')
    number, node_pattern = r'([-+0-9.eE]+)', r'(n\d+)'
    resistor = re.compile(r'I\(' + node_pattern + ', ' + node_pattern + r'\) <\+ V\(' + node_pattern + ', ' + node_pattern + r'\) / ' + number + ';')
    channel = re.compile(r'I\(' + node_pattern + ', ' + node_pattern + r'\) <\+ cpphdl_channel\(V\(' + node_pattern + ', ' + node_pattern + r'\), V\(' + node_pattern + ', ' + node_pattern + r'\), ' + number + ', ' + number + ', ' + number + r'\);')
    n = lambda beta: (beta, .7, 1)
    p = lambda beta: (beta, .7, -1)
    expected = {
        'cascode_ota.hpp': ([n(.03), n(.03), n(.06), n(.06), p(.03), p(.03), p(.06), p(.06), n(.06)], [100, 100, 200, 200, 300, 300, 600, 600, 200]),
        'inverter.hpp': ([p(.003), n(.003)], [30, 10]),
        'nmos_resistor_amplifier.hpp': ([n(.015), n(.015)], [50, 50]),
        'differential_receive_gain.hpp': ([n(.03)] * 6 + [item for _ in range(4) for item in (n(.03), p(.02))] + [n(.03)], [100] * 6 + [width for _ in range(4) for width in (100, 200)] + [100]),
        'receive_reference.hpp': ([n(.03)], [100])}
    groups, resistors, seen = defaultdict(list), [], set()
    identity = None
    normalize = lambda value: '0' if value == 'n0' else value
    for line in model.split(marker, 1)[1].removesuffix(suffix).splitlines():
        line = line.strip()
        match = annotation.fullmatch(line)
        if match:
            identity = match.groups()
            continue
        if not line or line.startswith('//'):
            continue
        if identity is None or identity[0] in seen or identity[0] in cap_ids:
            raise ValueError('missing, duplicate or overlapping preamplifier device identity')
        identifier, owner, filename = identity
        seen.add(identifier)
        identity = None
        match = resistor.fullmatch(line)
        if match:
            a, b, va, vb, value = match.groups()
            if (a, b) != (va, vb) or not math.isfinite(float(value)) or float(value) <= 0:
                raise ValueError('invalid preamplifier resistor law')
            resistors.append((identifier, normalize(a), normalize(b), float(value)))
            continue
        match = channel.fullmatch(line)
        if not match or owner is None:
            raise ValueError('unbound preamplifier contribution')
        a, b, gate, source, drain, drain_source, beta, threshold, polarity = match.groups()
        if (source, drain, drain_source) != (b, a, b):
            raise ValueError('changed preamplifier channel terminal references')
        groups[(owner, filename)].append((identifier, normalize(a), normalize(gate), normalize(b), tuple(map(float, (beta, threshold, polarity)))))
    if Counter(name for _, name in groups) != Counter({'cascode_ota.hpp': 2, 'inverter.hpp': 5,
            'nmos_resistor_amplifier.hpp': 1, 'nmos_source_follower.hpp': 4,
            'differential_receive_gain.hpp': 1, 'receive_reference.hpp': 1}):
        raise ValueError('changed preamplifier component hierarchy')
    supplies = [row for row in resistors if row[3] == .25]
    source = re.findall(r'^Vsource0 (n\d+) 0 DC ([^\n]+)$', original, re.M)
    if (len(supplies) != 1 or len(source) != 1 or not math.isclose(float(source[0][1]), 3.3)
            or supplies[0][1:3] != (source[0][0], observations['5']) or observations['5'] == 'n0'):
        raise ValueError('preamplifier supply observation does not identify its achieved rail')
    if Counter(x[3] for x in resistors) != Counter({.25: 1, 25: 2, 50: 7, 2000: 2, 1000: 4,
            330: 2, 15000: 2, 18000: 2, 23000: 2, 43000: 2, 2500: 2, 250: 3,
            100: 19, 500: 3, 150: 1, 100000: 1}):
        raise ValueError('changed preamplifier resistor inventory')
    devices, inventory, followers = [], [], Counter()
    for (owner, filename), parts in groups.items():
        parameters = [part[4] for part in parts]
        if filename == 'nmos_source_follower.hpp':
            choices = [(beta, width) for beta, width in ((.1, 400), (.2, 800), (.4, 1600)) if parameters == [n(beta)]]
            if len(choices) != 1:
                raise ValueError('unreviewed preamplifier follower')
            beta, width = choices[0]
            followers[beta] += 1
            wanted, widths = parameters, [width]
        else:
            wanted, widths = expected[filename]
        if len(parameters) != len(wanted) or any(not all(math.isclose(a, b, rel_tol=1e-12, abs_tol=1e-15) for a, b in zip(x, y)) for x, y in zip(parameters, wanted)):
            raise ValueError('changed preamplifier MOS role/order: ' + filename)
        for part, width in zip(parts, widths):
            identifier, drain, gate, source, parameters = part
            kind = 'nmos' if parameters[2] > 0 else 'pmos'
            body = '0' if kind == 'nmos' else normalize(observations['5'])
            devices.append(f'Xphysical{identifier} {drain} {gate} {source} {body} sg13_hv_{kind} w={width}u l=.45u')
            inventory.append(dict(id=identifier, owner=owner, source=filename, width_um=width, length_um=.45, model='sg13_hv_' + kind, body=body))
    if followers != Counter({.1: 2, .2: 1, .4: 1}) or len(devices) != 50:
        raise ValueError('changed complete preamplifier channel inventory')
    physical = [f'Rphysical{i} {a} {b} {value:.17g}' for i, a, b, value in resistors] + devices
    deck = [lines[0], f'.lib "{models / "cornerMOShv.lib"}" {corner}', f'.temp {temperature:g}']
    for line in lines[1:]:
        if line.startswith(('.model CircuitModel ', 'Ndesign ', 'pre_osdi ')):
            continue
        if line == '.control':
            deck += physical
        deck.append(line)
    result = '\n'.join(deck) + '\n'
    if any(token in result for token in ('cpphdl_channel', 'CircuitModel', 'design.osdi')):
        raise ValueError('generic preamplifier channel remains in process experiment')
    return result, dict(mos=inventory, mos_count=50, resistor_count=55, capacitor_count=62,
        scope='reviewed HV geometry and complete exported hierarchy/passives; nominal R/C, no mismatch/extracted layout')
