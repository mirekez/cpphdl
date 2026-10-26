"""Process devices for the independently LVS-checked oscillator geometry."""
from collections import Counter
import math

from processing_clock_oscillator_layout import PORTS


def netlist(geometry):
    """Preserve every physical MOS finger, resistor, MIM and antenna diode.

    This is the device-level view before wiring extraction. Substrate contacts
    are ideal here; it must not be used as evidence for wiring IR or substrate
    coupling. The actual PDK resistor models supply their own terminal charge.
    """
    lines = ['.subckt PhysicalOscillator ' + ' '.join(PORTS)]
    counts = Counter()
    for part in geometry['components']:
        name, kind, info, ports = (part[k] for k in ('name', 'kind', 'geometry', 'connections'))
        if kind == 'mos':
            model = info['model']
            if model not in ('sg13_lv_nmos', 'sg13_lv_pmos'):
                raise ValueError('unsupported oscillator MOS model')
            count = info['fingers']
            unit = dict(width_um=info['finger_width_um'], length_um=info['length_um'])
        elif kind == 'resistor':
            model = info['model']
            if model not in ('rppd', 'rhigh'):
                raise ValueError('unsupported oscillator resistance model')
            count, unit = info.get('units', 1), info.get('unit', info)
            if unit.get('bends', 0) != 0:
                raise ValueError('unqualified folded-resistor interpretation')
        elif kind == 'capacitor':
            model = 'cap_cmim'
            count, unit = info.get('capacitors', 1), info.get('unit', info)
        elif kind == 'diode':
            model, count, unit = info['model'], 1, info
            if model != 'dpantenna':
                raise ValueError('unsupported oscillator protection model')
        else:
            raise ValueError('unbound physical oscillator component')
        if not isinstance(count, int) or not 1 <= count <= 32:
            raise ValueError('unsupported physical device multiplicity')
        width, length = unit['width_um'], unit['length_um']
        if not all(math.isfinite(v) and v > 0 for v in (width, length)):
            raise ValueError('invalid physical device dimension')
        for index in range(count):
            if kind == 'mos':
                nodes = [ports[p] for p in ('D', 'G', 'S', 'B')]
            else:
                a = ports['PLUS'] if index == 0 else f'{name}_series_{index}'
                b = ports['MINUS'] if index + 1 == count else f'{name}_series_{index + 1}'
                nodes = [a, b] + ([ports['SUB']] if kind == 'resistor' else [])
            initial = ' ic=0' if kind == 'capacitor' else ''
            lines.append(f'X{name}_{index} ' + ' '.join(nodes) + f' {model} w={width:g}u l={length:g}u' + initial)
            counts[model] += 1
    expected = {'sg13_lv_nmos': 27, 'sg13_lv_pmos': 28, 'rppd': 33, 'rhigh': 32, 'cap_cmim': 60, 'dpantenna': 1}
    if dict(counts) != expected:
        raise ValueError('changed physical oscillator device inventory: ' + str(dict(counts)))
    lines.append('.ends PhysicalOscillator')
    return '\n'.join(lines) + '\n', dict(counts)
