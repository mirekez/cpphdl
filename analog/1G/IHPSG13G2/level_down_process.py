"""Explicit PSP geometry bindings for the four-MOS level-down circuit."""
from collections import Counter
import math
import re

# beta, threshold, sign, model, total width [um], length [um]
# These are chosen geometries, not an inferred beta-to-process conversion.
BINDINGS = (
    (.004, .7, 1, 'sg13_hv_nmos', 5, .45),
    (.0005, .35, -1, 'sg13_lv_pmos', 2, .13),
    (.01, .35, 1, 'sg13_lv_nmos', 10, .13),
    (.005, .35, -1, 'sg13_lv_pmos', 20, .13),
)


def process_deck(model, original, models, corner, temperature):
    if corner not in ('mos_tt', 'mos_ss', 'mos_ff', 'mos_sf', 'mos_fs'):
        raise ValueError('unknown IHP MOS corner')
    if not math.isfinite(temperature) or not -40 <= temperature <= 125:
        raise ValueError('unsupported process experiment temperature')
    marker, suffix = '    analog begin\n', '    end\nendmodule\n'
    if model.count(marker) != 1 or not model.endswith(suffix):
        raise ValueError('changed generated contribution block')
    body = model.split(marker, 1)[1].removesuffix(suffix)
    resistor = re.compile(r'I\((n\d+), (n\d+)\) <\+ V\((n\d+), (n\d+)\) / ([-+0-9.eE]+);')
    channel = re.compile(r'I\((n\d+), (n\d+)\) <\+ cpphdl_channel\(V\((n\d+), (n\d+)\), '
                         r'V\((n\d+), (n\d+)\), ([-+0-9.eE]+), ([-+0-9.eE]+), ([-+0-9.eE]+)\);')
    devices, inventory, passive = [], Counter(), []
    node = lambda value: '0' if value == 'n0' else value
    for line in body.splitlines():
        line = line.strip()
        if not line or line.startswith('//'):
            continue
        match = resistor.fullmatch(line)
        if match:
            a, b, va, vb, value = match.groups()
            if (a, b) != (va, vb) or float(value) not in (10, 50, 10000):
                raise ValueError('unexpected passive contribution')
            passive.append(float(value))
            devices.append(f'Rphysical{len(devices)} {node(a)} {node(b)} {value}')
            continue
        match = channel.fullmatch(line)
        if not match:
            raise ValueError('unbound analog law: ' + line)
        a, b, gate, source, drain, drain_source, beta, threshold, sign = match.groups()
        if source != b or (drain, drain_source) != (a, b):
            raise ValueError('channel terminals disagree with its branch')
        values = tuple(map(float, (beta, threshold, sign)))
        matches = [entry for entry in BINDINGS if all(math.isclose(x, y, rel_tol=1e-12, abs_tol=1e-14)
                   for x, y in zip(values, entry[:3]))]
        if len(matches) != 1:
            raise ValueError('no explicit process binding for channel')
        _, _, polarity, process, width, length = matches[0]
        inventory[process] += 1
        body_node = node(b) if polarity == -1 else '0'
        devices.append(f'Xphysical{len(devices)} {node(a)} {node(gate)} {node(b)} {body_node} '
                       f'{process} w={width}u l={length}u')
    if inventory != Counter(entry[3] for entry in BINDINGS) or sorted(passive) != [10, 50, 10000]:
        raise ValueError('changed four-MOS interface inventory')
    lines = original.splitlines()
    if (sum(line.startswith('Cdevice') for line in lines) != 9 or lines.count('.control') != 1
            or sum(line.startswith('Ndesign ') for line in lines) != 1):
        raise ValueError('changed host charge or model interface')
    deck = [lines[0], f'.lib "{models / "cornerMOSlv.lib"}" {corner}',
            f'.lib "{models / "cornerMOShv.lib"}" {corner}', f'.temp {temperature}']
    for line in lines[1:]:
        if line.startswith(('.model CircuitModel ', 'Ndesign ', 'pre_osdi ')):
            continue
        if line == '.control':
            deck.extend(devices)
        deck.append(line)
    result = '\n'.join(deck) + '\n'
    if any(token in result for token in ('CircuitModel', 'design.osdi', 'cpphdl_channel')):
        raise ValueError('generic channel remains in process deck')
    return result
