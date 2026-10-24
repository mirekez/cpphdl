"""Explicit process geometry for the independent loaded TRACK experiment.

The diagnostic runner supplies its tests directory on the module search path,
so the same exported topology/wiring validators guard process replacement.
"""
from collections import Counter
import math
import re

from core_driver_process import BINDINGS, contributions
from shared_receive_track_topology import inspect as topology
from shared_receive_track_wiring import inspect as wiring

_COUNTS = dict(logic_input_n=17, logic_input_p=17, logic_pull_down=34,
               cascode=34, latch=32, gate_buffer_n=32, gate_buffer_p=32,
               translation_replica=1)
TABLE = tuple((*entry[:-1], _COUNTS[entry[0]]) for entry in BINDINGS if entry[0] in _COUNTS)
TABLE += (
    ('strobe_latch', .0028, .7, -1, 'sg13_hv_pmos', 40, .45, 2),
    ('strobe_buffer_n', .006, .7, 1, 'sg13_hv_nmos', 40, .45, 3),
    ('strobe_buffer_p', .006, .7, -1, 'sg13_hv_pmos', 80, .45, 3),
    ('request_n', .008, .7, 1, 'sg13_hv_nmos', 20, .45, 130),
    ('request_p', .008, .7, -1, 'sg13_hv_pmos', 40, .45, 130),
    ('retention_n', .01, .7, 1, 'sg13_hv_nmos', 30, .45, 54),
    ('retention_p', .01, .7, -1, 'sg13_hv_pmos', 60, .45, 54),
    ('complement_n', .004, .7, 1, 'sg13_hv_nmos', 10, .45, 14),
    ('complement_p', .004, .7, -1, 'sg13_hv_pmos', 20, .45, 14),
    ('sampling_n', .03, .7, 1, 'sg13_hv_nmos', 37.5, .45, 13),
    ('sampling_p', .015, .7, -1, 'sg13_hv_pmos', 75, .45, 13),
    ('dummy_n', .016, .7, 1, 'sg13_hv_nmos', 20, .45, 13),
    ('dummy_p', .0084, .7, -1, 'sg13_hv_pmos', 42, .45, 13),
)


def process_deck(model, original, models, corner='mos_tt', temperature=27):
    if corner not in ('mos_tt', 'mos_ss', 'mos_ff') or not math.isfinite(temperature) or not -40 <= temperature <= 125:
        raise ValueError('unsupported TRACK process corner or temperature')
    topology_result, wiring_result = topology(model, original), wiring(model, original)
    devices, roles = contributions(model, TABLE)
    if roles != Counter({entry[0]: entry[-1] for entry in TABLE}):
        raise ValueError('changed physical TRACK geometry inventory')
    if Counter(d['value'] for d in devices if d['kind'] == 'R') != Counter({.25: 1, 10: 1, 50: 18, 200: 1, 800: 1, 10000: 1, 1000: 1}):
        raise ValueError('changed physical TRACK passive inventory')
    obs = dict(re.findall(r'^let observation(\d+) = v\((n\d+)\)$', original, re.M))
    vdd, core = obs['1'], obs['2']
    caps = [(a, b, float(value)) for a, b, value in re.findall(
        r'^Cdevice\d+ (\S+) (\S+) (\S+) IC=\S+$', original, re.M)]
    expected_caps = Counter({5e-15: 130, 1e-14: 114, 4e-14: 68, 5e-14: 60,
        6e-14: 34, 3.6e-14: 34, 7e-14: 34, 2.3e-14: 34, 1.6e-14: 34,
        2e-13: 34, 3e-15: 26, 2e-15: 26, 1.4e-14: 17, 2e-14: 14,
        1.5e-12: 13, 1e-15: 13, 2e-11: 2, 2.5e-11: 1, 1e-12: 1})
    if Counter(c[2] for c in caps) != expected_caps:
        raise ValueError('changed explicit TRACK storage inventory')
    for i in range(13):
        track, clock_n, held = (obs[str(6 + 7 * i + j)] for j in (1, 2, 3))
        for required in ((clock_n, '0', 20e-15), (track, held, 2e-15),
                         (clock_n, held, 2e-15), (obs['0'], held, 1e-15)):
            if caps.count(required) != 1:
                raise ValueError('sampler clock or input coupling changed')
    if caps.count((obs['166'], '0', 1e-12)) != 1:
        raise ValueError('event delay storage disconnected')
    delay_branches = [d for d in devices if d['kind'] == 'R' and d['value'] == 1000]
    if len(delay_branches) != 1 or {delay_branches[0]['a'], delay_branches[0]['b']} != {obs['166'], obs['5']}:
        raise ValueError('event delay resistance disconnected')
    lines = original.splitlines()
    if lines.count('.control') != 1 or sum(line.startswith('Ndesign ') for line in lines) != 1:
        raise ValueError('changed TRACK host interface')
    deck = [lines[0], f'.lib "{models / "cornerMOSlv.lib"}" {corner}',
            f'.lib "{models / "cornerMOShv.lib"}" {corner}', f'.temp {temperature}']
    fingers = dict(sampling_n=5, sampling_p=10, dummy_n=2, dummy_p=5)
    for line in lines[1:]:
        if line.startswith(('.model CircuitModel ', 'Ndesign ', 'pre_osdi ')):
            continue
        if line == '.control':
            for i, device in enumerate(devices):
                if device['kind'] == 'R':
                    deck.append(f'Rphysical{i} {device["a"]} {device["b"]} {device["value"]:.17g}')
                    continue
                device['body'] = (core if '_lv_' in device['model'] else vdd) if device['model'].endswith('pmos') else '0'
                count = fingers.get(device['role'], math.ceil(device['width_um'] / 10))
                device['fingers'] = count
                deck.append(f'Xphysical{i} {device["a"]} {device["gate"]} {device["b"]} {device["body"]} '
                            f'{device["model"]} w={device["width_um"]}u l={device["length_um"]}u ng={count}')
        deck.append(line)
    return '\n'.join(deck) + '\n', dict(devices=devices, roles=dict(roles),
        topology=topology_result, wiring=wiring_result,
        scope='explicit loaded TRACK geometry, not bank conversion or routed clock qualification')
