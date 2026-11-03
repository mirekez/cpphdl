"""Physical shared replica bias, including the transmitter's declared storage."""
from collections import Counter
from pathlib import Path
import re

from capacitor_bank_layout import append_mim_bank
from core_driver_process import BINDINGS, contributions
from mos_layout import append_mos
from reference_clock_row import create_row
from transmit_translation_bias_routes import add_storage_routes
from resistor_layout import append_rppd

PORTS = ['VDD', 'VSS', 'VLOGIC', 'BIAS']


def circuit(source):
    text = Path(source).read_text()
    if 'module OneGTransmitTranslationBias(' + ', '.join(PORTS) + ');' not in text:
        raise ValueError('requires the C++ transmitter replica export')
    private = sorted(set(re.findall(r'\bcpphdl_n\d+\b', text)))
    if len(private) != 1:
        raise ValueError('replica divider node inventory changed')
    names = ['VSS', 'VDD', 'VLOGIC', 'BIAS', *private]
    mapping = {name: 'n' + str(i) for i, name in enumerate(names)}
    text = re.sub(r'\b(?:' + '|'.join(names) + r')\b', lambda m: mapping[m[0]], text)
    pattern = re.compile(r'^\s*I\((n\d+), (n\d+)\) <\+ ([-+0-9.eE]+) \* '
        r'ddt\(V\((n\d+), (n\d+)\)\); // Bench initial voltage = 0.0 V\.$', re.M)
    storage = pattern.findall(text)
    if (len(storage) != 1 or storage[0][:2] != ('n3', 'n0') or
            storage[0][3:] != ('n3', 'n0') or float(storage[0][2]) != 50e-12):
        raise ValueError('shared replica storage changed')
    bindings = tuple(row for row in BINDINGS if row[0] == 'translation_replica')
    devices, roles = contributions(pattern.sub('', text), bindings)
    if roles != Counter(translation_replica=1) or len(devices) != 4:
        raise ValueError('replica device inventory changed')
    rename = {'0': 'VSS', **{mapping[p]: p for p in PORTS}, 'n4': 'DIVIDED'}
    for device in devices:
        for key in ('a', 'b', 'gate', 'body'):
            if key in device:
                device[key] = rename[device[key]]
    branches = {(frozenset((d['a'], d['b'])), d['value']) for d in devices if d['kind'] == 'R'}
    if branches != {(frozenset(('DIVIDED', 'VLOGIC')), 200),
                    (frozenset(('DIVIDED', 'VSS')), 800), (frozenset(('BIAS', 'VDD')), 10000)}:
        raise ValueError('replica resistor topology changed')
    transistor = next(d for d in devices if d['kind'] == 'X')
    if tuple(transistor[k] for k in ('a', 'gate', 'b', 'body')) != ('BIAS', 'BIAS', 'DIVIDED', 'VSS'):
        raise ValueError('replica transistor topology changed')
    return devices, float(storage[0][2])


def create_translation_bias(pdk, source):
    devices, storage = circuit(source)

    def parts(layout):
        result = []
        for index, device in enumerate(devices):
            if device['kind'] == 'R':
                cell, info = append_rppd(layout, pdk, 'ReplicaResistance' + str(index),
                    device['value'], width_um=2)
                binding, kind = dict(PLUS=device['a'], MINUS=device['b'], SUB='VSS'), 'resistor'
            else:
                cell, info = append_mos(layout, pdk, 'ReplicaMOS', device['width_um'],
                    device['length_um'], 20, 'n', 'hv')
                binding = dict(D=device['a'], G=device['gate'], S=device['b'], B=device['body'])
                kind = 'mos'
            result.append((cell, info, binding, kind))
        cell, info = append_mim_bank(layout, pdk, 'ReplicaStorage', storage)
        result.append((cell, info, dict(PLUS='BIAS', MINUS='VSS'), 'capacitor'))
        return result

    layout, top, info = create_row('TransmitTranslationBias', PORTS, parts, priority_nets=('BIAS', 'DIVIDED'),
                                  via_columns=2, signal_width_um=2)
    add_storage_routes(layout, top, info)
    return layout, top, info
