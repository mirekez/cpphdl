"""Physical implementation of the exported coarse/fine processing oscillator."""
from collections import Counter, defaultdict
import math
from pathlib import Path
import re

from analog_row_layout import create_row
from antenna_layout import append_antenna
from capacitor_layout import append_mim
from core_driver_process import contributions
from mos_layout import append_mos
from oscillator_passive_layout import append_one_ff, append_fine_resistor
from processing_clock_process import BINDINGS
from resistor_layout import append_rppd

PORTS = ['VDD', 'VSS', 'CLEAR', 'OUT'] + [f'{kind}_N{bit}' for bit in range(8) for kind in ('COARSE', 'FINE')]


def circuit(source):
    text = Path(source).read_text()
    names = ['Vdd', 'Vss', *PORTS[2:]]
    if 'module OneGIHPProcessingClockOscillator(' + ', '.join(names) + ');' not in text:
        raise ValueError('requires the exported processing oscillator')
    names = ['Vss', 'Vdd', *PORTS[2:]] + sorted(set(re.findall(r'\bcpphdl_n\d+\b', text)))
    mapping = {name: 'n' + str(i) for i, name in enumerate(names)}
    text = re.sub(r'\b(?:' + '|'.join(names) + r')\b', lambda m: mapping[m[0]], text)
    capacitor = re.compile(r'^\s*I\((n\d+), (n\d+)\) <\+ ([-+0-9.eE]+) \* '
                           r'ddt\(V\((n\d+), (n\d+)\)\); // Bench initial voltage = 0.0 V\.$', re.M)
    capacitors = defaultdict(float)
    for match in capacitor.finditer(text):
        a, b, value, va, vb = match.groups()
        if (a, b) != (va, vb) or a == b or not math.isfinite(float(value)) or float(value) <= 0:
            raise ValueError('invalid oscillator storage law')
        capacitors[a, b] += float(value)
    devices, roles = contributions(capacitor.sub('', text), BINDINGS)
    if roles != Counter({row[0]: row[-1] for row in BINDINGS}):
        raise ValueError('changed oscillator transistor inventory')
    resistances = Counter(d['value'] for d in devices if d['kind'] == 'R')
    if resistances != Counter({10000.: 15, 20000.: 18, 5090000.: 1}):
        raise ValueError('changed oscillator resistance inventory')
    if len(capacitors) != 46:
        raise ValueError('changed oscillator storage inventory: ' + str(len(capacitors)))
    rename = {mapping[name]: name.upper() for name in names[:20]}
    rename['0'] = 'VSS'

    def net(name):
        return rename.get(name, name.upper())

    for device in devices:
        for key in ('a', 'b', 'gate', 'body'):
            if key in device:
                device[key] = net(device[key])
    return devices, [(net(a), net(b), value) for (a, b), value in sorted(capacitors.items())]


def create_oscillator(pdk, source):
    devices, capacitors = circuit(source)

    def parts(layout):
        result = []
        for index, device in enumerate(devices):
            if device['kind'] == 'R':
                name = 'OscillatorResistance' + str(index)
                if device['value'] > 1e6:
                    cell, info = append_fine_resistor(layout, pdk, name, device['value'])
                else:
                    # These bias branches carry at most 1.2 V / 10 kohm.
                    # Narrow legal poly reduces its real substrate charge.
                    cell, info = append_rppd(layout, pdk, name, device['value'], width_um=.5)
                connections = dict(PLUS=device['a'], MINUS=device['b'], SUB='VSS')
                kind = 'resistor'
            else:
                width = device['width_um']
                cell, info = append_mos(layout, pdk, 'OscillatorMOS' + str(index), width, device['length_um'],
                    math.ceil(width / 10), 'p' if device['model'].endswith('pmos') else 'n', 'lv')
                connections = dict(D=device['a'], G=device['gate'], S=device['b'], B=device['body'])
                kind = 'mos'
            result.append((cell, info, connections, kind))
        for index, (a, b, value) in enumerate(capacitors):
            make = append_one_ff if value < 2e-15 else append_mim
            cell, info = make(layout, pdk, 'OscillatorStorage' + str(index), value)
            result.append((cell, info, dict(PLUS=a, MINUS=b), 'capacitor'))
        # The weak fine/coarse bias net has no diffusion connection to relieve
        # fabrication charge. This explicit physical correction contributes
        # real junction capacitance/leakage to subsequent process simulation.
        fine = next(device for device in devices if device['kind'] == 'R' and device['value'] > 1e6)
        cell, info = append_antenna(layout, pdk, 'ControlAntenna')
        result.append((cell, info, dict(PLUS=fine['b'], MINUS='VDD'), 'diode'))
        return result

    layout, top, info = create_row('ProcessingClockOscillator', PORTS, parts)
    info['intended_device_inventory'] = dict(MOS=51, resistance_branches=34, storage_branches=len(capacitors))
    info['physical_corrections'] = ['one P-diffusion/N-well antenna diode from CONTROL to VDD, including its contacted guard']
    return layout, top, info
