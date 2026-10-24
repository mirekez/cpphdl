"""Explicit SG13G2 geometry for a core-controlled receive DAC experiment."""
from collections import Counter
import math
import re

from core_driver_process import BINDINGS, contributions


TRANSLATION_ROLES = {'logic_input_n', 'logic_input_p', 'logic_pull_down', 'cascode',
    'latch', 'gate_buffer_n', 'gate_buffer_p', 'translation_replica'}
TABLE = tuple((*entry[:-1], 1 if entry[0] == 'translation_replica' else entry[-1] // 2)
    for entry in BINDINGS if entry[0] in TRANSLATION_ROLES)
TABLE = tuple((entry[0], 2 * entry[1], *entry[2:5], 2 * entry[5], *entry[6:])
    if entry[0] in ('latch', 'gate_buffer_n', 'gate_buffer_p') else entry for entry in TABLE)
# Half-width ladder drivers reduce translator loading; their higher on
# resistance must still satisfy the absolute DAC accuracy check.
TABLE += (('dac_n', .04, .7, 1, 'sg13_hv_nmos', 100, .45, 8),
          ('dac_p', .04, .7, -1, 'sg13_hv_pmos', 200, .45, 8))


def process_deck(model, original, models, corner='mos_tt', temperature=27):
    if corner not in ('mos_tt', 'mos_ss', 'mos_ff', 'mos_sf', 'mos_fs') or not math.isfinite(temperature) or not -40 <= temperature <= 125:
        raise ValueError('unsupported receive DAC corner or temperature')
    devices, roles = contributions(model, TABLE)
    if roles != Counter({entry[0]: entry[-1] for entry in TABLE}):
        raise ValueError('receive DAC MOS inventory changed')
    resistors = [d for d in devices if d['kind'] == 'R']
    if Counter(d['value'] for d in resistors) != Counter({.25: 1, 10: 1, 50: 8, 200: 1, 800: 1, 10000: 1, 750: 7, 1500: 9}):
        raise ValueError('receive DAC resistor inventory changed')
    lines = original.splitlines()
    observations = dict(re.findall(r'^let observation(\d+) = v\((n\d+)\)$', original, re.M))
    if set(observations) != set(map(str, range(44))) or lines.count('.control') != 1 or sum(line.startswith('Ndesign ') for line in lines) != 1:
        raise ValueError('receive DAC experiment interface changed')
    by_role = {role: [d for d in devices if d.get('role') == role] for role in roles}
    vdd, core, bias = (observations[str(i)] for i in (1, 2, 3))
    trials = [observations[str(4 + 5 * i)] for i in range(8)]
    bit_n = [observations[str(5 + 5 * i)] for i in range(8)]
    if len(set(trials + bit_n)) != 16:
        raise ValueError('aliased receive DAC controls')
    if (Counter(d['gate'] for d in by_role['logic_input_n']) != Counter(trials) or
            Counter(d['gate'] for d in by_role['logic_input_p']) != Counter(trials) or
            any(d['b'] != core for d in by_role['logic_input_p']) or
            any(d['b'] != vdd for role in ('latch', 'gate_buffer_p', 'dac_p') for d in by_role[role]) or
            any(d['gate'] != bias for d in by_role['cascode'])):
        raise ValueError('receive DAC input, supply or shared-bias topology changed')
    for bit, (trial, output) in enumerate(zip(trials, bit_n)):
        input_n = [d for d in by_role['logic_input_n'] if d['gate'] == trial]
        buffer_n = [d for d in by_role['gate_buffer_n'] if d['a'] == output]
        n_dac = [d for d in by_role['dac_n'] if d['gate'] == output]
        p_dac = [d for d in by_role['dac_p'] if d['gate'] == output]
        if not all(len(parts) == 1 for parts in (input_n, buffer_n, n_dac, p_dac)):
            raise ValueError('receive DAC bit is not connected to a physical translated driver')
        cascodes = [d for d in by_role['cascode'] if d['a'] == buffer_n[0]['gate']]
        if (len(cascodes) != 1 or n_dac[0]['a'] != p_dac[0]['a'] or
                len([d for d in by_role['logic_pull_down'] if d['a'] == cascodes[0]['b'] and d['gate'] == input_n[0]['a']]) != 1):
            raise ValueError('receive DAC translated complement or ladder driver changed')
        if (input_n[0]['a'] != observations[str(6 + 5 * bit)] or
                cascodes[0]['b'] != observations[str(7 + 5 * bit)] or
                len([d for d in by_role['logic_pull_down'] if d['gate'] == trial and
                    d['a'] == observations[str(8 + 5 * bit)]]) != 1):
            raise ValueError('receive DAC core-device exposure probes changed')
    caps = [line for line in lines if line.startswith('Cdevice')]
    pattern = re.compile(r'Cdevice(\d+) (\S+) (\S+) ([-+0-9.eE]+) IC=([-+0-9.eE]+)')
    parsed = [pattern.fullmatch(line) for line in caps]
    identifiers = re.findall(r'// Capacitor device (\d+) is stamped once by the SPICE host deck[.]', model)
    if (len(caps) != 212 or any(m is None for m in parsed) or
            Counter(m[1] for m in parsed) != Counter(identifiers) or
            any(not math.isfinite(float(m[4])) or float(m[4]) <= 0 or float(m[5]) != 0 for m in parsed) or
            len([m for m in parsed if {m[2], m[3]} == {bias, '0'} and math.isclose(float(m[4]), 25e-12, rel_tol=1e-12, abs_tol=0)]) != 1):
        raise ValueError('receive DAC cold storage or shared-bias capacitor changed')
    deck = [lines[0], f'.lib "{models / "cornerMOSlv.lib"}" {corner}',
        f'.lib "{models / "cornerMOShv.lib"}" {corner}', f'.temp {temperature}']
    for line in lines[1:]:
        if line.startswith(('.model CircuitModel ', 'Ndesign ', 'pre_osdi ')):
            continue
        if line == '.control':
            for index, d in enumerate(devices):
                if d['kind'] == 'R':
                    deck.append(f'Rphysical{index} {d["a"]} {d["b"]} {d["value"]:.17g}')
                else:
                    deck.append(f'Xphysical{index} {d["a"]} {d["gate"]} {d["b"]} {d["body"]} '
                        f'{d["model"]} w={d["width_um"]}u l={d["length_um"]}u')
        deck.append(line)
    return '\n'.join(deck) + '\n', dict(roles=dict(roles), devices=devices,
        scope='eight real core-to-HV translators, shared replica and loaded R/2R ladder; folded PDK fingers required')
