"""Reviewed SG13G2 devices for the connected processing oscillator and admission gate."""
from collections import Counter
import math
import re

from core_driver_process import contributions
from mos_geometry import size_mos_instances

# Role, generic-law marker, threshold, polarity, PSP device, total W/um, L/um, count.
BINDINGS = (
    ('dac_n', .002, .35, 1, 'sg13_lv_nmos', 2, .13, 16),
    ('dac_p', .002, .35, -1, 'sg13_lv_pmos', 4, .13, 16),
    ('ring_n', .001, .35, 1, 'sg13_lv_nmos', 1, .13, 7),
    ('ring_p', .001, .35, -1, 'sg13_lv_pmos', 2, .13, 7),
    ('clear_n', .01, .35, 1, 'sg13_lv_nmos', 10, .13, 1),
    ('logic_n', .004, .35, 1, 'sg13_lv_nmos', 4, .13, 18),
    ('logic_p', .004, .35, -1, 'sg13_lv_pmos', 8, .13, 18),
    ('driver_n', .02, .35, 1, 'sg13_lv_nmos', 20, .13, 3),
    ('driver_p', .02, .35, -1, 'sg13_lv_pmos', 40, .13, 3),
)


def process_deck(model, original, models, corner='mos_tt', temperature=27):
    if corner not in ('mos_tt', 'mos_ss', 'mos_ff', 'mos_sf', 'mos_fs'):
        raise ValueError('unknown IHP MOS corner')
    if not math.isfinite(temperature) or not -40 <= temperature <= 125:
        raise ValueError('unsupported oscillator temperature')
    devices, roles = contributions(model, BINDINGS)
    if roles != Counter({entry[0]: entry[-1] for entry in BINDINGS}):
        raise ValueError('processing clock MOS inventory differs from reviewed dimensions')
    observations = re.findall(r'^let observation(\d+) = (v\(n\d+\))$', original, re.M)
    if [int(number) for number, _ in observations] != list(range(8)):
        raise ValueError('processing clock observation interface changed')
    lines = original.splitlines()
    if lines.count('.control') != 1 or len(re.findall(r'^Ndesign ', original, re.M)) != 1:
        raise ValueError('unexpected generated oscillator experiment')
    output = [lines[0], f'.lib "{models / "cornerMOSlv.lib"}" {corner}', f'.temp {temperature:g}']
    for line in lines[1:]:
        if line.startswith(('.model CircuitModel ', 'Ndesign ', 'pre_osdi ')):
            continue
        if line == '.control':
            for index, device in enumerate(devices):
                if device['kind'] == 'R':
                    output.append(f'Rphysical{index} {device["a"]} {device["b"]} {device["value"]:.17g}')
                else:
                    output.append(f'Xphysical{index} {device["a"]} {device["gate"]} {device["b"]} '
                                  f'{device["body"]} {device["model"]} '
                                  f'w={device["width_um"]}u l={device["length_um"]}u')
            output.append('.save ' + ' '.join(node for _, node in observations))
        output.append(line)
        if line == '.control':
            output.append('set num_threads = 1')
    deck, geometry = size_mos_instances('\n'.join(output) + '\n')
    if any(token in deck for token in ('CircuitModel', 'design.osdi', 'cpphdl_channel')):
        raise ValueError('generic channel remains in processing clock process circuit')
    return deck, dict(roles=dict(roles), geometry=geometry,
                      scope='connected DAC, ring, buffers and admission gate with 89 PSP MOS; declared external R/C')
