"""Reviewed IHP geometries for the low-headroom cascoded transmitter."""
from collections import Counter
import math
import re

from core_driver_process import PROGRAMMABLE_BINDINGS, contributions


BINDINGS = tuple(entry for entry in PROGRAMMABLE_BINDINGS if entry[0] != 'steering') + (
    ('main_steering', .08, .7, 1, 'sg13_hv_nmos', 400, .45, 8),
    ('history_steering', .04, .7, 1, 'sg13_hv_nmos', 200, .45, 8),
    ('main_current_cascode', .3, .7, 1, 'sg13_hv_nmos', 1500, .45, 4),
    ('history_current_cascode', .1, .7, 1, 'sg13_hv_nmos', 500, .45, 4),
)


def process_deck(model, original, models, corner, *, programmable=True, temperature=27):
    if not programmable or corner not in ('mos_tt', 'mos_ss', 'mos_ff', 'mos_sf', 'mos_fs'):
        raise ValueError('unsupported cascoded transmitter profile')
    if not math.isfinite(temperature) or not -40 <= temperature <= 125:
        raise ValueError('unsupported cascoded transmitter temperature')
    devices, roles = contributions(model, BINDINGS)
    if roles != Counter({entry[0]: entry[-1] for entry in BINDINGS}):
        raise ValueError('changed cascoded transmitter MOS inventory')
    resistors = [device for device in devices if device['kind'] == 'R']
    if len(resistors) != 39:
        raise ValueError('changed cascoded transmitter resistor inventory')
    gates, lower_nodes, output_nodes = set(), set(), set()
    bias = {d['gate'] for d in devices if d.get('role') in ('main_sink', 'history_sink')}
    if len(bias) != 1:
        raise ValueError('current sinks do not share their reference')
    for bank in ('main', 'history'):
        sinks = [d for d in devices if d.get('role') == bank + '_sink']
        cascodes = [d for d in devices if d.get('role') == bank + '_current_cascode']
        steering = [d for d in devices if d.get('role') == bank + '_steering']
        if len({d['a'] for d in sinks}) != 4 or any(d['b'] != '0' for d in sinks):
            raise ValueError('changed current-setting sink terminals')
        for cascode in cascodes:
            if sum(d['a'] == cascode['b'] for d in sinks) != 1:
                raise ValueError('cascode does not isolate its current-setting drain')
            branches = [d for d in steering if d['b'] == cascode['a']]
            if len(branches) != 2 or len({d['a'] for d in branches}) != 2 or len({d['gate'] for d in branches}) != 2:
                raise ValueError('changed differential steering pair')
            gates.add(cascode['gate'])
            lower_nodes.add(cascode['b'])
            output_nodes.update(d['a'] for d in branches)
    if len(gates) != 1 or len(lower_nodes) != 8 or len(output_nodes) != 2:
        raise ValueError('aliased cascoded transmitter nodes')
    gate = next(iter(gates))
    top = [d for d in resistors if d['value'] == 3000 and gate in (d['a'], d['b'])]
    bottom = [d for d in resistors if d['value'] == 3600 and {d['a'], d['b']} == {gate, '0'}]
    if len(top) != 1 or len(bottom) != 1:
        raise ValueError('changed low-headroom cascode divider')
    lines = original.splitlines()
    capacitors = [line for line in lines if line.startswith('Cdevice')]
    if len(capacitors) != 615:
        raise ValueError('changed cascoded transmitter charge inventory')
    for node, value in [(next(iter(bias)), 400e-12), (gate, 50e-12)]:
        found = [line.split() for line in capacitors if set(line.split()[1:3]) == {node, '0'}
                 and math.isclose(float(line.split()[3]), value, rel_tol=1e-12)]
        if len(found) != 1:
            raise ValueError('changed bias-filter storage')
    if lines.count('.control') != 1 or sum(line.startswith('Ndesign ') for line in lines) != 1:
        raise ValueError('changed cascoded transmitter host interface')
    deck = [lines[0], f'.lib "{models / "cornerMOSlv.lib"}" {corner}',
            f'.lib "{models / "cornerMOShv.lib"}" {corner}', f'.temp {temperature}']
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
    if re.search(r'CircuitModel|design[.]osdi|cpphdl_channel', result):
        raise ValueError('generic law remains in cascoded process deck')
    return result, dict(roles=dict(roles), devices=devices,
                        scope='reviewed 304-MOS cascode and steering geometry, 39 resistors and 615 explicit capacitors; nominal passive models')
