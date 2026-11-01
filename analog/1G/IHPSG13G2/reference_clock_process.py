"""Explicit SG13G2 geometry for the connected PLL, buffer and clock translator.

The C++ IC supplies topology and external storage. These reviewed dimensions
select PSP devices; beta is an IR identifier, not a geometry conversion rule.
"""
from collections import Counter
import math
import re

from core_driver_process import contributions
from mos_geometry import size_mos_instances

# Role, generic beta, threshold, polarity, PSP model, total W/um, L/um, count.
BINDINGS = (
    ('logic_n', .002, .35, 1, 'sg13_lv_nmos', 2, .13, 94),
    ('logic_p', .002, .35, -1, 'sg13_lv_pmos', 4, .13, 93),
    ('clear_n', .01, .35, 1, 'sg13_lv_nmos', 10, .13, 6),
    ('clear_p', .01, .35, -1, 'sg13_lv_pmos', 20, .13, 6),
    ('ring_n', .001, .35, 1, 'sg13_lv_nmos', 1, .13, 7),
    ('ring_p', .001, .35, -1, 'sg13_lv_pmos', 2, .13, 7),
    ('predriver_n', .004, .35, 1, 'sg13_lv_nmos', 4, .13, 1),
    ('predriver_p', .004, .35, -1, 'sg13_lv_pmos', 8, .13, 1),
    ('driver_n', .02, .35, 1, 'sg13_lv_nmos', 20, .13, 1),
    ('driver_p', .02, .35, -1, 'sg13_lv_pmos', 40, .13, 1),
    ('translator_input_n', .02, .5, 1, 'sg13_lv_nmos', 10, .13, 1),
    ('translator_input_p', .02, .5, -1, 'sg13_lv_pmos', 20, .13, 1),
    ('translator_pull_down', .06, .5, 1, 'sg13_lv_nmos', 40, .13, 2),
    ('translator_cascode', .015, .7, 1, 'sg13_hv_nmos', 80, .45, 2),
    ('translator_latch', .0014, .7, -1, 'sg13_hv_pmos', 20, .45, 2),
    ('translator_output_n', .003, .7, 1, 'sg13_hv_nmos', 20, .45, 2),
    ('translator_output_p', .003, .7, -1, 'sg13_hv_pmos', 40, .45, 2),
    ('translator_replica', .01875, .7, 1, 'sg13_hv_nmos', 100, .45, 1),
)


def process_deck(model, original, models, corner='mos_tt', temperature=27):
    if corner not in ('mos_tt', 'mos_ss', 'mos_ff', 'mos_sf', 'mos_fs'):
        raise ValueError('unknown IHP MOS corner')
    if not math.isfinite(temperature) or not -40 <= temperature <= 125:
        raise ValueError('unsupported clock temperature')
    devices, roles = contributions(model, BINDINGS)
    if roles != Counter({entry[0]: entry[-1] for entry in BINDINGS}):
        raise ValueError('clock MOS inventory differs from reviewed dimensions')
    resistances = Counter(device['value'] for device in devices if device['kind'] == 'R')
    # The experiment includes two 10-ohm supply and two 50-ohm input drivers.
    if resistances != Counter({10000.: 4, 1000.: 2, 2000.: 1, 200.: 1, 800.: 1, 10.: 2, 50.: 2}):
        raise ValueError('clock resistance inventory differs from the physical candidate')
    observations = re.findall(r'^let observation(\d+) = (v\(n\d+\))$', original, re.M)
    if [int(number) for number, _ in observations] != list(range(13)):
        raise ValueError('clock observation interface changed')
    if original.splitlines().count('.control') != 1 or len(re.findall(r'^Ndesign ', original, re.M)) != 1:
        raise ValueError('unexpected generated clock experiment')
    lines = original.splitlines()
    output = [lines[0], f'.lib "{models / "cornerMOSlv.lib"}" {corner}',
              f'.lib "{models / "cornerMOShv.lib"}" {corner}', f'.temp {temperature:g}']
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
            # Limit storage to the complete observed waveform, not every PSP
            # internal variable. This does not change equations or integration.
            output.append('.save ' + ' '.join(node for _, node in observations))
        output.append(line)
        if line == '.control':
            output.append('set num_threads = 1')
    deck, geometry = size_mos_instances('\n'.join(output) + '\n')
    if any(token in deck for token in ('CircuitModel', 'design.osdi', 'cpphdl_channel')):
        raise ValueError('generic channel remains in clock process circuit')
    return deck, dict(roles=dict(roles), geometry=geometry, resistances=dict(resistances),
                      scope='connected PLL and translator with actual PSP MOS; declared external R/C')
