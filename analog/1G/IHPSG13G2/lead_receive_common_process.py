"""Reviewed IHP binding for the shared sink and eight loaded input networks."""
from collections import Counter, defaultdict
import math
import re


def process_deck(model, original, models, corner='mos_tt', temperature=27):
    if corner not in ('mos_tt', 'mos_ss', 'mos_ff') or not math.isfinite(temperature) or not -40 <= temperature <= 125:
        raise ValueError('unsupported common-mode process condition')
    marker, suffix = '    analog begin\n', '    end\nendmodule\n'
    if model.count(marker) != 1 or not model.endswith(suffix):
        raise ValueError('changed common-mode contribution block')
    lines = original.splitlines()
    observations = dict(re.findall(r'^let observation(\d+) = v\((n\d+)\)$', original, re.M))
    if set(observations) != set(map(str, range(39))) or lines.count('.control') != 1:
        raise ValueError('changed shared common-mode observations')
    caps = re.findall(r'^Cdevice(\d+) (0|n\d+) (0|n\d+) (\S+) IC=(\S+)$', original, re.M)
    expected_caps = Counter({1.5e-13: 16, 4e-14: 8, 1e-11: 4, 2e-11: 1, 1e-14: 2,
        1.6e-11: 1, 8e-11: 1, 2e-12: 1, 2.5e-13: 1, 8e-14: 1, 2e-14: 1})
    cap_ids = re.findall(r'// Capacitor device (\d+) is stamped once by the SPICE host deck[.]', model)
    if (len(caps) != 37 or Counter(float(c[3]) for c in caps) != expected_caps or
            Counter(c[0] for c in caps) != Counter(cap_ids) or any(float(c[4]) != 0 for c in caps)):
        raise ValueError('changed shared common-mode storage or cold charge')
    annotation = re.compile(r'// device (\d+), (?:IC ([^,]+), )?.*/([^/]+[.](?:hpp|cpp)):\d+')
    node, number = r'(n\d+)', r'([-+0-9.eE]+)'
    resistor = re.compile(r'I\(' + node + ', ' + node + r'\) <\+ V\(' + node + ', ' + node + r'\) / ' + number + ';')
    channel = re.compile(r'I\(' + node + ', ' + node + r'\) <\+ cpphdl_channel\(V\(' + node + ', ' + node + r'\), V\('
        + node + ', ' + node + r'\), ' + number + ', ' + number + ', ' + number + r'\);')
    resistors, groups, seen = [], defaultdict(list), set()
    identity = None
    for line in model.split(marker, 1)[1].removesuffix(suffix).splitlines():
        line = line.strip()
        match = annotation.fullmatch(line)
        if match:
            identity = match.groups()
            continue
        if not line or line.startswith('//'):
            continue
        if identity is None or identity[0] in seen or identity[0] in cap_ids:
            raise ValueError('missing or duplicate common-mode contribution')
        identifier, owner, filename = identity
        identity = None
        seen.add(identifier)
        match = resistor.fullmatch(line)
        if match:
            a, b, va, vb, value = match.groups()
            if (a, b) != (va, vb) or not math.isfinite(float(value)):
                raise ValueError('changed common-mode resistor terminal law')
            resistors.append((identifier, a, b, float(value)))
            continue
        match = channel.fullmatch(line)
        if match is None or owner is None:
            raise ValueError('unbound common-mode physical law')
        drain, source, gate, source_ref, drain_ref, second_source, beta, threshold, polarity = match.groups()
        if (source_ref, drain_ref, second_source) != (source, drain, source):
            raise ValueError('changed common-mode channel terminals')
        groups[(owner, filename)].append((identifier, drain, gate, source, tuple(map(float, (beta, threshold, polarity)))))
    expected_r = Counter({25: 16, 500: 16, 150: 16, 100: 8, 12000: 1, 12150: 1, .25: 1, 10000: 1,
        21000: 1, 23000: 1, 43000: 1, 2500: 1, 1000: 2})
    if len(resistors) != 66 or Counter(r[3] for r in resistors) != expected_r:
        raise ValueError('changed common-mode resistor inventory')
    if Counter(name for _, name in groups) != Counter({'cascode_ota.hpp': 1, 'shunt_voltage_buffer.hpp': 1, 'lead_receive_common_reference.hpp': 1}):
        raise ValueError('changed common-mode hierarchy')
    by_file = {name: parts for (_, name), parts in groups.items()}
    n = lambda beta: (beta, .7, 1)
    p = lambda beta: (beta, .7, -1)
    expected = {'cascode_ota.hpp': ([n(.03), n(.03), n(.06), n(.06), p(.03), p(.03), p(.06), p(.06), n(.06)],
        [100, 100, 200, 200, 300, 300, 600, 600, 200]),
        'shunt_voltage_buffer.hpp': ([n(.03)], [150]), 'lead_receive_common_reference.hpp': ([n(.03)], [100])}
    ota, sink, tail = (by_file[name] for name in ('cascode_ota.hpp', 'shunt_voltage_buffer.hpp', 'lead_receive_common_reference.hpp'))
    if (ota[0][2] != observations['0'] or ota[1][2] != observations['2'] or ota[3][1] != observations['3'] or
            ota[2][2] != observations['4'] or ota[6][2] != observations['5'] or ota[8][2] != observations['6'] or
            sink[0][1:4] != (observations['0'], observations['3'], 'n0') or
            tail[0][1:4] != (observations['6'], observations['6'], 'n0')):
        raise ValueError('changed shunt feedback, bias or sink connections')
    lead_resistors = [r for r in resistors if r[3] == 1000 and observations['3'] in r[1:3]]
    if len(lead_resistors) != 1 or observations['3'] not in lead_resistors[0][1:3]:
        raise ValueError('missing series compensation resistor at DRIVE')
    lead_node = next(node for node in lead_resistors[0][1:3] if node != observations['3'])
    lead_caps = [cap for cap in caps if float(cap[3]) == 2e-12]
    if len(lead_caps) != 1 or set(lead_caps[0][1:3]) != {lead_node, observations['0']}:
        raise ValueError('changed series compensation capacitor connection')
    output_caps = [cap for cap in caps if float(cap[3]) == 8e-11]
    if len(output_caps) != 1 or set(output_caps[0][1:3]) != {'0', observations['0']}:
        raise ValueError('changed shunt output storage connection')
    for lane in range(8):
        line_p, line_n, plus, minus = (observations[str(7 + 4 * lane + i)] for i in range(4))
        for a, b, value in ((line_p, plus, 500), (line_n, minus, 500),
                (plus, observations['0'], 150), (minus, observations['0'], 150), (plus, minus, 100)):
            if len([r for r in resistors if r[3] == value and set(r[1:3]) == {a, b}]) != 1:
                raise ValueError('changed connected input-network topology')
    source = re.findall(r'^Vsource0 (n\d+) 0 PWL\(', original, re.M)
    supply_resistors = [r for r in resistors if r[3] == .25]
    if len(source) != 1 or len(supply_resistors) != 1 or supply_resistors[0][1:3] != (source[0], observations['1']):
        raise ValueError('common-mode achieved supply is not identified')
    for a, b, value in ((observations['1'], observations['2'], 10000),
                         (observations['2'], 'n0', 12150)):
        if len([r for r in resistors if r[3] == value and set(r[1:3]) == {a, b}]) != 1:
            raise ValueError('changed declared target divider topology or ratio')
    normalize = lambda v: '0' if v == 'n0' else v
    physical, inventory = [], []
    for (_, filename), parts in groups.items():
        parameters, widths = expected[filename]
        if len(parts) != len(parameters) or any(not all(math.isclose(x, y, rel_tol=1e-12, abs_tol=1e-15)
                for x, y in zip(part[4], want)) for part, want in zip(parts, parameters)):
            raise ValueError('unreviewed common-mode transistor profile')
        for part, width in zip(parts, widths):
            identifier, drain, gate, source_node, values = part
            kind = 'nmos' if values[2] > 0 else 'pmos'
            body = '0' if kind == 'nmos' else observations['1']
            physical.append(f'Xphysical{identifier} {normalize(drain)} {normalize(gate)} {normalize(source_node)} {body} sg13_hv_{kind} w={width}u l=.45u')
            inventory.append(dict(id=identifier, source=filename, width_um=width, length_um=.45, model='sg13_hv_' + kind, body=body))
    physical += [f'Rphysical{i} {normalize(a)} {normalize(b)} {v:.17g}' for i, a, b, v in resistors]
    deck = [lines[0], f'.lib "{models / "cornerMOShv.lib"}" {corner}', f'.temp {temperature:g}']
    for line in lines[1:]:
        if line.startswith(('.model CircuitModel ', 'Ndesign ', 'pre_osdi ')):
            continue
        if line == '.control':
            deck += physical
        deck.append(line)
    result = '\n'.join(deck) + '\n'
    if any(token in result for token in ('cpphdl_channel', 'CircuitModel', 'design.osdi')):
        raise ValueError('generic common-mode channel remains in physical deck')
    return result, dict(mos=inventory, mos_count=11, resistor_count=66, capacitor_count=37,
        scope='reviewed HV devices, eight finite-source passive input networks, nominal R/C and cold initial charge; no gain stage, ADC, pad/ESD or full PHY qualification')
