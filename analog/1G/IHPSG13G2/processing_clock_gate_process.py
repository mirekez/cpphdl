"""Explicit SG13G2 transistor geometry for the physical clock admission gate."""
from collections import Counter
import re

from core_driver_process import contributions
from mos_geometry import size_mos_instances

BINDINGS = (
    ('logic_n', .004, .35, 1, 'sg13_lv_nmos', 4, .13, 17),
    ('logic_p', .004, .35, -1, 'sg13_lv_pmos', 8, .13, 17),
    ('strong_n', .02, .35, 1, 'sg13_lv_nmos', 20, .13, 2),
    ('strong_p', .02, .35, -1, 'sg13_lv_pmos', 40, .13, 2),
)


def process_deck(model, original, models, corner, temperature, supply):
    if (corner, temperature, supply) not in (
            ('mos_tt', 27, 1.2), ('mos_ss', 125, 1.08), ('mos_ff', -40, 1.32)):
        raise ValueError('undeclared clock admission PVT condition')
    devices, roles = contributions(model, BINDINGS)
    if roles != Counter({row[0]: row[-1] for row in BINDINGS}):
        raise ValueError('changed clock admission transistor inventory')
    if Counter(d['value'] for d in devices if d['kind'] == 'R') != Counter({1.0: 1, 50.0: 2}):
        raise ValueError('changed clock admission source impedances or hidden weak return')
    if len(re.findall(r'^Cdevice', original, re.M)) != 25:
        raise ValueError('changed clock admission storage')
    if original.count('.control\n') != 1 or len(re.findall(r'^Ndesign ', original, re.M)) != 1:
        raise ValueError('changed clock admission experiment interface')
    output = [original.splitlines()[0], f'.lib "{models / "cornerMOSlv.lib"}" {corner}',
              f'.temp {temperature}']
    sources = 0
    for line in original.splitlines()[1:]:
        if line.startswith(('.model CircuitModel ', 'Ndesign ', 'pre_osdi ')):
            continue
        if line.startswith('Vsource'):
            match = re.fullmatch(r'(Vsource\d+ n\d+ 0 PWL\()([^()]+)(\))', line)
            if not match:
                raise ValueError('unsupported clock admission voltage source')
            points = match[2].split()
            if len(points) % 2 or any(float(v) not in (0, 1.2) for v in points[1::2]):
                raise ValueError('unexpected clock admission stimulus')
            points[1::2] = [str(supply if float(v) else 0) for v in points[1::2]]
            line = match[1] + ' '.join(points) + match[3]
            sources += 1
        if line == '.control':
            for index, device in enumerate(devices):
                if device['kind'] == 'R':
                    output.append(f'Rphysical{index} {device["a"]} {device["b"]} {device["value"]:.17g}')
                else:
                    output.append(f'Xphysical{index} {device["a"]} {device["gate"]} {device["b"]} '
                                  f'{device["body"]} {device["model"]} '
                                  f'w={device["width_um"]}u l={device["length_um"]}u')
        output.append(line)
    if sources != 3:
        raise ValueError('missing source, raw clock or admission request')
    deck, geometry = size_mos_instances('\n'.join(output) + '\n')
    if any(token in deck for token in ('CircuitModel', 'design.osdi', 'cpphdl_channel')):
        raise ValueError('generic channel remains in clock admission process circuit')
    return deck, dict(roles=dict(roles), geometry=geometry, supply_V=supply,
                      corner=corner, temperature_C=temperature)
