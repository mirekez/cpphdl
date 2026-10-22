"""Explicit IHP bindings for the exported core-to-MDI transmit circuit.

Only the channel/resistor subset emitted for this circuit is accepted. This is
not a general Verilog-A parser or an automatic beta-to-geometry calculation.
"""
from collections import Counter
import math
import re


# role, beta, threshold, polarity, IHP model, total W [um], L [um], count
BINDINGS = (
    ('logic_input_n', .02, .5, 1, 'sg13_lv_nmos', 10, .13, 16),
    ('logic_input_p', .02, .5, -1, 'sg13_lv_pmos', 20, .13, 16),
    ('logic_pull_down', .06, .5, 1, 'sg13_lv_nmos', 40, .13, 32),
    ('cascode', .015, .7, 1, 'sg13_hv_nmos', 80, .45, 32),
    ('latch', .0014, .7, -1, 'sg13_hv_pmos', 20, .45, 32),
    ('gate_buffer_n', .003, .7, 1, 'sg13_hv_nmos', 20, .45, 32),
    ('gate_buffer_p', .003, .7, -1, 'sg13_hv_pmos', 40, .45, 32),
    ('translation_replica', .01875, .7, 1, 'sg13_hv_nmos', 100, .45, 1),
    ('steering', .02, .7, 1, 'sg13_hv_nmos', 100, .45, 16),
    ('current_reference', .014268727705112964, .35, 1, 'sg13_hv_nmos', 100, .45, 1),
    ('main_sink', .0659862954984239, .35, 1, 'sg13_hv_nmos', 450, .45, 4),
    ('history_sink', .021995431832807968, .35, 1, 'sg13_hv_nmos', 150, .45, 4),
)


def contributions(model):
    marker = '    analog begin\n'
    if model.count(marker) != 1:
        raise ValueError('expected one generated analog contribution block')
    body = model.split(marker, 1)[1]
    if not body.endswith('    end\nendmodule\n'):
        raise ValueError('generated module terminator changed')
    body = body.removesuffix('    end\nendmodule\n')
    resistor = re.compile(r'I\((n\d+), (n\d+)\) <\+ V\((n\d+), (n\d+)\) / ([-+0-9.eE]+);')
    channel = re.compile(r'I\((n\d+), (n\d+)\) <\+ cpphdl_channel\(V\((n\d+), (n\d+)\), '
                         r'V\((n\d+), (n\d+)\), ([-+0-9.eE]+), ([-+0-9.eE]+), ([-+0-9.eE]+)\);')
    devices, roles = [], Counter()
    node = lambda value: '0' if value == 'n0' else value
    for line in body.splitlines():
        line = line.strip()
        if not line or line.startswith('//'):
            continue
        match = resistor.fullmatch(line)
        if match:
            a, b, va, vb, value = match.groups()
            if (a, b) != (va, vb) or not math.isfinite(float(value)) or float(value) <= 0:
                raise ValueError('invalid resistor contribution')
            devices.append(dict(kind='R', a=node(a), b=node(b), value=float(value)))
            continue
        match = channel.fullmatch(line)
        if not match:
            raise ValueError('unbound generated analog contribution: ' + line)
        a, b, gate, source, drain, drain_source, beta, threshold, polarity = match.groups()
        if source != b or (drain, drain_source) != (a, b):
            raise ValueError('channel terminal references do not match its branch')
        values = list(map(float, (beta, threshold, polarity)))
        if not all(math.isfinite(value) for value in values):
            raise ValueError('nonfinite channel parameters')
        matches = [entry for entry in BINDINGS if all(math.isclose(x, y, rel_tol=1e-12, abs_tol=1e-14)
                    for x, y in zip(values, entry[1:4]))]
        if len(matches) != 1:
            raise ValueError('channel has no unique explicit IHP geometry binding')
        role, _, _, sign, process, width, length, _ = matches[0]
        roles[role] += 1
        devices.append(dict(kind='X', a=node(a), b=node(b), gate=node(gate),
                            body=node(b) if sign == -1 else '0', role=role,
                            model=process, width_um=width, length_um=length))
    return devices, roles


def process_deck(model, original, models, corner):
    if corner not in ('mos_tt', 'mos_ss', 'mos_ff', 'mos_sf', 'mos_fs'):
        raise ValueError('unknown IHP MOS corner')
    devices, roles = contributions(model)
    if roles != Counter({entry[0]: entry[-1] for entry in BINDINGS}):
        raise ValueError('core transmit circuit no longer has its explicitly sized 218-MOS inventory')
    if sum(device['kind'] == 'R' for device in devices) != 25:
        raise ValueError('core transmit passive inventory changed')
    lines = original.splitlines()
    capacitors = [line for line in lines if line.startswith('Cdevice')]
    if len(capacitors) != 446:
        raise ValueError('review changed explicit storage before process qualification')
    if lines.count('.control') != 1 or sum(line.startswith('Ndesign ') for line in lines) != 1:
        raise ValueError('unexpected host experiment interface')
    deck = [lines[0], f'.lib "{models / "cornerMOSlv.lib"}" {corner}',
            f'.lib "{models / "cornerMOShv.lib"}" {corner}', '.temp 27']
    for line in lines[1:]:
        if line.startswith(('.model CircuitModel ', 'Ndesign ', 'pre_osdi ')):
            continue
        if line == '.control':
            for index, device in enumerate(devices):
                if device['kind'] == 'R':
                    deck.append(f'Rphysical{index} {device["a"]} {device["b"]} {device["value"]:.17g}')
                else:
                    deck.append(f'Xphysical{index} {device["a"]} {device["gate"]} {device["b"]} '
                                f'{device["body"]} {device["model"]} '
                                f'w={device["width_um"]}u l={device["length_um"]}u')
        deck.append(line)
    result = '\n'.join(deck) + '\n'
    if any(token in result for token in ('CircuitModel', 'design.osdi', 'cpphdl_channel')):
        raise ValueError('generic channel remains in the IHP process deck')
    return result, dict(roles=dict(roles), devices=devices,
                        scope='explicit geometry binding; total widths are folded into valid fingers separately')
