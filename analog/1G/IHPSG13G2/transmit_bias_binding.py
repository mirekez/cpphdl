"""Bind the exported transmitter's trim reference to its physical bias macro."""
from collections import Counter
import math
from pathlib import Path
import re

from core_driver_process import PROGRAMMABLE_BINDINGS, contributions
from transmit_bias_layout import PORTS


def bind_current_bias(source, geometry):
    text = Path(source).read_text()
    if 'module OneGTransmitCurrentBias(' + ', '.join(PORTS) + ');' not in text:
        raise ValueError('unexpected transmitter bias export interface')
    private = sorted(set(re.findall(r'\bcpphdl_n\d+\b', text)))
    if len(private) != 6:
        raise ValueError('transmitter bias must retain six private trim branches')
    names = ['VSS', *[p for p in PORTS if p != 'VSS'], *private]
    mapping = {name: 'n' + str(index) for index, name in enumerate(names)}
    text = re.sub(r'\b(?:' + '|'.join(names) + r')\b', lambda m: mapping[m[0]], text)
    devices, roles = contributions(text, PROGRAMMABLE_BINDINGS)
    if len(devices) != 16 or roles != Counter(current_reference=1, trim_switch=6):
        raise ValueError('changed transmitter bias device inventory')
    rename = {node: name for name, node in mapping.items()} | {'0': 'VSS'}
    # Infer branch identity from each actual bit's switch gate, independently
    # of export numbering and placement order.
    branches = {}
    for device in devices:
        if device.get('role') == 'trim_switch':
            gate = rename[device['gate']]
            branch = 'BRANCH' + gate.removeprefix('TRIM_N')
            if gate not in PORTS[4:] or branch in branches.values():
                raise ValueError('aliased trim-switch command')
            branches[rename[device['a']]] = branch
    if len(branches) != 6 or len(set(branches.values())) != 6:
        raise ValueError('trim branch topology changed')

    def net(node):
        name = rename[node]
        return branches.get(name, name)

    expected = []
    for device in devices:
        if device['kind'] == 'R':
            expected.append(('resistor', tuple(sorted((net(device['a']), net(device['b'])))), device['value']))
        else:
            expected.append(('mos', tuple(net(device[p]) for p in ('a', 'gate', 'b', 'body')),
                             device['model'], device['width_um'], device['length_um']))
    physical = []
    for part in geometry['components']:
        g, nodes = part['geometry'], part['connections']
        if part['kind'] == 'resistor':
            if set(nodes) != {'PLUS', 'MINUS', 'SUB'} or nodes['SUB'] != 'VSS':
                raise ValueError('physical bias resistor body changed')
            physical.append(('resistor', tuple(sorted((nodes['PLUS'], nodes['MINUS']))), g['requested_resistance_Ohm']))
        elif part['kind'] == 'mos':
            if set(nodes) != {'D', 'G', 'S', 'B'}:
                raise ValueError('physical bias MOS terminal inventory changed')
            physical.append(('mos', tuple(nodes[p] for p in ('D', 'G', 'S', 'B')),
                             g['model'], g['total_width_um'], g['length_um']))
        else:
            raise ValueError('unexpected physical bias component')
    if set(geometry['terminals']) != set(PORTS) or Counter(expected) != Counter(physical):
        raise ValueError('C++ bias connections or dimensions differ from physical macro')
    if any(not math.isfinite(float(item[-1])) or item[-1] <= 0 for item in physical):
        raise ValueError('nonpositive physical bias parameter')
    return dict(devices=16, mos=7, resistors=9, trim_branches=6,
                scope='exact exported topology and reviewed geometry binding; loaded electrical checks separate')
