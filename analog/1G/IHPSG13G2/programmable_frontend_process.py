"""Explicit reviewed PSP geometries for the buffered, terminated one-slice receiver."""
from collections import Counter, defaultdict
import math
import re
from receive_frontend_process import selections, match_parameters

NUMBER = r'([-+0-9.eE]+)'
NODE = r'(n\d+)'


def process_deck(model, original, models, corner='mos_tt', temperature=27):
    if corner not in ('mos_tt', 'mos_ss', 'mos_ff', 'mos_sf', 'mos_fs') or not -40 <= temperature <= 125:
        raise ValueError('unsupported programmable receiver corner or temperature')
    marker, suffix = '    analog begin\n', '    end\nendmodule\n'
    if model.count(marker) != 1 or not model.endswith(suffix):
        raise ValueError('changed programmable receiver contribution block')
    lines = original.splitlines()
    observations = dict(re.findall(r'^let observation(\d+) = v\((n\d+)\)$', original, re.M))
    if (set(observations) != set(map(str, range(9))) or lines.count('.control') != 1
            or sum(line.startswith('Ndesign ') for line in lines) != 1):
        raise ValueError('changed programmable receiver physical interface')
    plus, minus, gain_input, gain_output, held, track, vdd, core, gain_bias = (observations[str(i)] for i in range(9))
    annotation = re.compile(r'// device (\d+), IC ([^,]+), .*/([^/]+[.]hpp):\d+')
    resistor = re.compile(r'I\(' + NODE + ', ' + NODE + r'\) <\+ V\(' + NODE + ', ' + NODE + r'\) / ' + NUMBER + ';')
    channel = re.compile(r'I\(' + NODE + ', ' + NODE + r'\) <\+ cpphdl_channel\(V\(' + NODE + ', ' + NODE + r'\), V\('
                         + NODE + ', ' + NODE + r'\), ' + NUMBER + ', ' + NUMBER + ', ' + NUMBER + r'\);')
    owner = filename = identifier = None
    groups, passives, consumed = defaultdict(list), [], set()
    node = lambda value: '0' if value == 'n0' else value
    allowed_resistances = {.25, 10, 50, 100, 150, 250, 500, 1000, 2000, 2500,
                          15000, 18000, 23000, 43000, 100000, 1e9}
    for line in model.split(marker, 1)[1].removesuffix(suffix).splitlines():
        line = line.strip()
        match = annotation.fullmatch(line)
        if match:
            identifier, owner, filename = match.groups()
            continue
        if line.startswith('// device '):
            identifier = re.match(r'// device (\d+), ', line)[1]
            owner = filename = None
        if not line or line.startswith('//'):
            continue
        if identifier is None or identifier in consumed:
            raise ValueError('unidentified or duplicate programmable receiver device')
        consumed.add(identifier)
        match = resistor.fullmatch(line)
        if match:
            a, b, va, vb, value = match.groups()
            if (a, b) != (va, vb) or float(value) not in allowed_resistances:
                raise ValueError('unbound programmable receiver resistor')
            passives.append((identifier, a, b, value))
            continue
        match = channel.fullmatch(line)
        if not match or owner is None:
            raise ValueError('unbound programmable receiver physical law')
        a, b, gate, source, drain, drain_source, beta, threshold, polarity = match.groups()
        if (source, drain, drain_source) != (b, a, b):
            raise ValueError('changed programmable receiver MOS terminal references')
        groups[(owner, filename)].append(dict(identifier=identifier, drain=a, source=b, gate=gate,
            parameters=tuple(map(float, (beta, threshold, polarity)))))
    expected = Counter({('nmos_resistor_amplifier.hpp', 2): 3, ('nmos_source_follower.hpp', 1): 3,
        ('programmable_receive_gain.hpp', 10): 1, ('cascode_opamp.hpp', 9): 1,
        ('mos_track_hold.hpp', 4): 1, ('strongarm_comparator.hpp', 11): 1,
        ('cmos_logic.hpp', 4): 2, ('nand_latch.hpp', 2): 1,
        ('inverter.hpp', 2): 14, ('level_down.hpp', 4): 1})
    if Counter((filename, len(parts)) for (_, filename), parts in groups.items()) != expected or len(passives) != 66:
        raise ValueError('changed programmable receiver device inventory')
    cap_ids = re.findall(r'// Capacitor device (\d+) is stamped once by the SPICE host deck[.]', model)
    capacitors = [line for line in lines if line.startswith('Cdevice')]
    parsed_caps = [re.fullmatch(r'Cdevice(\d+) (?:0|n\d+) (?:0|n\d+) ' + NUMBER + ' IC=' + NUMBER, line) for line in capacitors]
    if (len(capacitors) != 105 or any(match is None for match in parsed_caps)
            or Counter(match[1] for match in parsed_caps) != Counter(cap_ids)
            or any(not math.isfinite(float(match[i])) for match in parsed_caps for i in (2, 3))
            or any(float(match[2]) <= 0 for match in parsed_caps)):
        raise ValueError('changed or missing programmable receiver storage')
    inputs = [parts for (_, filename), parts in groups.items() if filename == 'nmos_resistor_amplifier.hpp'
              and [part['gate'] for part in parts] == [plus, minus]]
    input_buffers = [parts[0] for (_, filename), parts in groups.items() if filename == 'nmos_source_follower.hpp'
                     and parts[0]['source'] == gain_input]
    cascodes = [parts for (_, filename), parts in groups.items() if filename == 'cascode_opamp.hpp'][0]
    outputs = [parts[0] for (_, filename), parts in groups.items() if filename == 'nmos_source_follower.hpp'
               and parts[0]['source'] == gain_output]
    if (len(inputs) != 2 or [p['drain'] for p in inputs[0]] != [p['drain'] for p in inputs[1]]
            or len(input_buffers) != 2 or any(p['drain'] != vdd or p['gate'] != inputs[0][1]['drain'] for p in input_buffers)
            or len(outputs) != 1 or outputs[0]['drain'] != vdd or outputs[0]['gate'] != cascodes[3]['drain']
            or cascodes[0]['gate'] != gain_bias
            or len([r for r in passives if float(r[3]) == 100 and set(r[1:3]) == {plus, minus}]) != 1):
        raise ValueError('changed termination, input isolation or gain-output topology')
    switch = next(parts for (_, filename), parts in groups.items() if filename == 'mos_track_hold.hpp')
    if (len({gain_output, held, track}) != 3 or switch[0]['gate'] != track
            or switch[0]['drain'] != gain_output or switch[0]['source'] != held
            or switch[1]['drain'] != held or switch[1]['source'] != gain_output
            or switch[2]['drain'] != held or switch[2]['source'] != held
            or switch[2]['gate'] != switch[1]['gate'] or switch[3]['drain'] != held
            or switch[3]['source'] != held or switch[3]['gate'] != track):
        raise ValueError('changed programmable sampler clock/input/dummy topology')
    cap_fields = [line.split() for line in capacitors]
    for a, b, value, initial in ((held, 'n0', 1.5e-12, 1.65),
            (track, held, 2e-15, -1.65), (switch[1]['gate'], held, 2e-15, 1.65),
            (gain_output, held, 1e-15, 0)):
        matches = [c for c in cap_fields if c[1:3] == [node(a), node(b)]
                   and math.isclose(float(c[3]), value, rel_tol=1e-12, abs_tol=1e-25)]
        if len(matches) != 1 or not math.isclose(float(matches[0][4][3:]), initial, abs_tol=1e-12):
            raise ValueError('changed programmable sampler storage/coupling/initial charge')
    devices, inventory = [], []
    n = lambda beta: (beta, .7, 1)
    p = lambda beta: (beta, .7, -1)
    for (owner, filename), parts in groups.items():
        parameters = [part['parameters'] for part in parts]
        if filename == 'programmable_receive_gain.hpp':
            if not match_parameters(parameters, [n(.03)] + [p(.02)] * 9):
                raise ValueError('changed programmable feedback/reference MOS')
            widths = [100] + [200] * 9
        elif filename == 'cascode_opamp.hpp':
            if not match_parameters(parameters, [n(.03)] * 2 + [n(.06)] * 2 + [p(.03)] * 2 + [p(.06)] * 2 + [n(.06)]):
                raise ValueError('changed cascode amplifier MOS')
            widths = [100, 100, 200, 200, 300, 300, 600, 600, 200]
        elif filename == 'nmos_source_follower.hpp' and parts[0]['source'] == gain_output:
            if not match_parameters(parameters, [n(.2)]):
                raise ValueError('changed gain output buffer')
            widths = [800]
        elif filename == 'mos_track_hold.hpp':
            if not match_parameters(parameters, [n(.02), p(.01), n(.01), p(.005)]):
                raise ValueError('changed programmable sampler MOS')
            widths = [25, 50, 12.5, 25]
        else:
            widths = selections(filename, parameters)
        for part, width in zip(parts, widths):
            _, threshold, polarity = part['parameters']
            low_voltage = math.isclose(threshold, .35)
            kind = ('lv' if low_voltage else 'hv') + ('_nmos' if polarity > 0 else '_pmos')
            body = '0' if polarity > 0 else node(core if low_voltage else vdd)
            length = .13 if low_voltage else .45
            devices.append(f'Xphysical{part["identifier"]} {node(part["drain"])} {node(part["gate"])} '
                           f'{node(part["source"])} {body} sg13_{kind} w={width}u l={length}u'
                           + (' ng=5' if width == 25 else ''))
            inventory.append(dict(instance=owner, device=part['identifier'], source=filename,
                width_um=width, length_um=length, model='sg13_' + kind, body=body))
    deck = [lines[0], f'.lib "{models / "cornerMOShv.lib"}" {corner}',
            f'.lib "{models / "cornerMOSlv.lib"}" {corner}', f'.temp {temperature}']
    for line in lines[1:]:
        if line.startswith(('.model CircuitModel ', 'Ndesign ', 'pre_osdi ')):
            continue
        if line == '.control':
            deck += [f'Rphysical{identifier} {node(a)} {node(b)} {value}' for identifier, a, b, value in passives] + devices
        deck.append(line)
    result = '\n'.join(deck) + '\n'
    if any(token in result for token in ('CircuitModel', 'design.osdi', 'cpphdl_channel')):
        raise ValueError('generic channel remains in programmable receiver process deck')
    return result, inventory
