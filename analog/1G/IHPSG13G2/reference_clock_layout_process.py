"""PSP/R3/MIM devices corresponding to the reference-clock layout geometry."""
from collections import Counter
import math
import re

PORTS = ['VDD', 'VSS', 'VLOGIC', 'REF', 'CLEAR', 'OUT', 'OUT_N']
HEADER = ['LOW', 'OUT', 'OUT_N', 'DIVIDED', 'REF', 'CONTROL', 'VLOGIC', 'VDD',
          'BIAS', 'UP', 'DOWN', 'PREDRIVE', 'BUFFERED']


def observations(model, probes):
    if list(probes) != HEADER:
        raise ValueError('changed C++ clock observation contract')
    names = ['Vss', 'Vdd', 'Vlogic', *PORTS[3:]] + sorted(set(re.findall(r'\bcpphdl_n\d+\b', model)))
    rename = {name: name.upper() if i < len(PORTS) else 'N' + str(i) for i, name in enumerate(names)}
    if any(name not in rename for name in probes.values()):
        raise ValueError('clock probe is not a physical circuit node')
    return {name: rename[node] for name, node in probes.items()}


def netlist(geometry):
    lines = ['.subckt PhysicalReferenceClock ' + ' '.join(PORTS)]
    inventory = Counter()
    for part in geometry['components']:
        name, kind, info, ports = (part[key] for key in ('name', 'kind', 'geometry', 'connections'))
        parallel = False
        if kind == 'mos':
            model = info['model']
            if model not in ('sg13_lv_nmos', 'sg13_lv_pmos', 'sg13_hv_nmos', 'sg13_hv_pmos'):
                raise ValueError('unbound reference-clock MOS')
            count, width, length = info['fingers'], info['finger_width_um'], info['length_um']
        elif kind == 'resistor':
            model, count = info['model'], 1
            if model != 'rppd' or info.get('bends', 0) or info.get('units', 1) != 1:
                raise ValueError('unbound reference-clock resistance')
            width, length = info['width_um'], info['length_um']
        elif kind == 'capacitor':
            model, count = 'cap_cmim', info.get('capacitors', 1)
            unit = info.get('unit', info)
            width, length = unit['width_um'], unit['length_um']
            parallel = 'columns' in info
            if count > 1 and not parallel and not any(key in info for key in ('junction_via_um', 'junction_vias_um')):
                raise ValueError('missing capacitor series/parallel connectivity')
        elif kind == 'diode':
            model, count = info['model'], 1
            if model != 'dpantenna':
                raise ValueError('unbound reference-clock protection diode')
            width, length = info['width_um'], info['length_um']
        else:
            raise ValueError('unknown physical reference-clock device')
        if not isinstance(count, int) or not 1 <= count <= 32 or not all(math.isfinite(v) and v > 0 for v in (width, length)):
            raise ValueError('invalid physical clock dimensions or multiplicity')
        for index in range(count):
            if kind == 'mos':
                nodes = [ports[key] for key in ('D', 'G', 'S', 'B')]
            else:
                a = ports['PLUS'] if parallel or index == 0 else f'{name}_series_{index}'
                b = ports['MINUS'] if parallel or index + 1 == count else f'{name}_series_{index + 1}'
                nodes = [a, b] + ([ports['SUB']] if kind == 'resistor' else [])
            initial = ' ic=0' if kind == 'capacitor' else ''
            lines.append(f'X{name}_{index} ' + ' '.join(nodes) + f' {model} w={width:g}u l={length:g}u' + initial)
            inventory[model] += 1
    expected = {'sg13_lv_nmos': 119, 'sg13_lv_pmos': 119, 'sg13_hv_nmos': 30,
                'sg13_hv_pmos': 12, 'rppd': 9, 'cap_cmim': 174, 'dpantenna': 3}
    if dict(inventory) != expected:
        raise ValueError('changed physical reference-clock inventory: ' + str(dict(inventory)))
    lines.append('.ends PhysicalReferenceClock')
    return '\n'.join(lines) + '\n', dict(inventory)
