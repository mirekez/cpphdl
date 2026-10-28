"""Replace complete C++ clock ICs through exported ownership and port boundaries.

This is an explicit process realization. The surrounding analog topology and
its declared storage remain from C++ export; the selected sequential blocks
use the independently tested PDK circuits. It is not a physical layout proof.
"""
from collections import Counter
import math
import re

from clock_cell_library import definitions, library_path
from core_driver_process import contributions
from mos_geometry import size_mos_instances
from reference_clock_cells import CELLS as DETECTOR_CELLS, detector
from reference_clock_divider_cells import CELLS as DIVIDER_CELLS, divider
from reference_clock_process import BINDINGS
from reference_clock_layout_process import PORTS

BLOCKS = {
    'detector': ('VDD VSS REF FEEDBACK CLEAR UP UP_N DOWN DOWN_N', 219),
    'divider': ('VDD VSS CLOCK CLEAR Q Q_N', 101),
}
CAPACITOR = re.compile(r'I\((n\d+), (n\d+)\) <\+ ([-+0-9.eE]+) \* '
                       r'ddt\(V\((n\d+), (n\d+)\)\); // Bench initial voltage = 0.0 V\.')


def partition(model, realizations):
    if set(realizations) != set(BLOCKS):
        raise ValueError('both complete clock realization boundaries are required')
    names = ['Vss', 'Vdd', 'Vlogic', *PORTS[3:]]
    names += sorted(set(re.findall(r'\bcpphdl_n\d+\b', model)))
    ports = ['Vdd', 'Vss', 'Vlogic', *PORTS[3:]]
    if 'module OneGIHPReferenceClockGenerator(' + ', '.join(ports) + ');' not in model:
        raise ValueError('unexpected reference-clock export interface')
    records = list(re.finditer(r'^\s*// device (\d+),[^\n]*\n[ \t]*(I\([^\n]+)', model, re.M))
    ids = [int(match[1]) for match in records]
    if not records or len(ids) != len(set(ids)) or len(records) != len(re.findall(r'^\s*I\(', model, re.M)):
        raise ValueError('missing or duplicate C++ device identities')
    owner = {}
    for block, (formal, count) in BLOCKS.items():
        item = realizations[block]
        if set(item) != {'ports', 'elements'} or list(item['ports']) != formal.split():
            raise ValueError('changed clock realization interface: ' + block)
        if (any(node not in names for node in item['ports'].values()) or
                len(set(item['ports'].values())) != len(item['ports'])):
            raise ValueError('clock realization port names an absent C++ node')
        elements = item['elements']
        if len(elements) != count or any(type(index) is not int or index not in ids or index in owner for index in elements):
            raise ValueError('overlapping, missing or changed clock ownership inventory')
        if len(set(elements)) != count:
            raise ValueError('duplicate owned clock element')
        owner.update({index: block for index in elements})
    nodes = {}
    for match in records:
        nodes[int(match[1])] = set(re.findall(r'\b(?:Vss|Vdd|Vlogic|REF|CLEAR|OUT|OUT_N|cpphdl_n\d+)\b', match[2]))
    for block, item in realizations.items():
        inside = set().union(*(nodes[index] for index in item['elements']))
        outside = set().union(*(value for index, value in nodes.items() if owner.get(index) != block))
        ports = set(item['ports'].values())
        if not ports <= inside or (inside - ports) & outside:
            raise ValueError('clock realization leaks an undeclared internal node: ' + block)
    # Remove contributions by C++ identity before any parallel capacitors are
    # merged. Shared terminal capacitance owned by the surrounding IC remains.
    remaining = model
    for match in reversed(records):
        if int(match[1]) in owner:
            remaining = remaining[:match.start()] + '\n' + remaining[match.end():]
    mapping = {name: 'n' + str(index) for index, name in enumerate(names)}
    node_pattern = re.compile(r'\b(?:' + '|'.join(names) + r')\b')
    mapped_full = node_pattern.sub(lambda match: mapping[match[0]], model)
    # Validate the complete original circuit before selecting its remainder.
    original_devices, original_roles = contributions(CAPACITOR.sub('', mapped_full), BINDINGS)
    if original_roles != Counter({row[0]: row[-1] for row in BINDINGS}):
        raise ValueError('unreviewed complete reference-clock transistor inventory')
    if Counter(device['value'] for device in original_devices if device['kind'] == 'R') != Counter(
            {10000.: 4, 1000.: 2, 2000.: 1, 200.: 1, 800.: 1}):
        raise ValueError('unreviewed complete reference-clock resistance inventory')
    mapped = node_pattern.sub(lambda match: mapping[match[0]], remaining)
    capacitors = []
    for match in CAPACITOR.finditer(mapped):
        a, b, value, va, vb = match.groups()
        if (a, b) != (va, vb) or a == b or not math.isfinite(float(value)) or float(value) <= 0:
            raise ValueError('invalid retained clock storage contribution')
        capacitors.append((a, b, float(value)))
    devices, roles = contributions(CAPACITOR.sub('', mapped), BINDINGS)
    rename = {'n' + str(index): name.upper() for index, name in enumerate(names[:len(PORTS)])}
    rename['0'] = 'VSS'
    node = lambda name: rename.get(name, name.upper())
    for device in devices:
        for key in ('a', 'b', 'gate', 'body'):
            if key in device:
                device[key] = node(device[key])
    bound = {name: {port: node(mapping[value]) for port, value in item['ports'].items()}
             for name, item in realizations.items()}
    return devices, [(node(a), node(b), value) for a, b, value in capacitors], bound, dict(
        removed_elements={name: len(item['elements']) for name, item in realizations.items()},
        remaining_roles=dict(roles), remaining_nonstorage_devices=len(devices),
        original_nonstorage_devices=len(original_devices), retained_storage_branches=len(capacitors),
        undeclared_boundary_connections=0)


def netlist(model, realizations, pdk):
    devices, capacitors, blocks, audit = partition(model, realizations)
    cells = dict(DETECTOR_CELLS)
    for name, spec in DIVIDER_CELLS.items():
        if name in cells and cells[name] != spec:
            raise ValueError('inconsistent detector/divider library interfaces')
        cells[name] = spec
    circuit = [definitions(library_path(pdk).read_text(), cells), detector(), divider(),
               '.subckt PhysicalReferenceClock ' + ' '.join(PORTS)]
    for index, device in enumerate(devices):
        if device['kind'] == 'R':
            circuit.append(f'Rremaining{index} {device["a"]} {device["b"]} {device["value"]:.17g}')
        else:
            circuit.append(f'Xremaining{index} {device["a"]} {device["gate"]} {device["b"]} '
                           f'{device["body"]} {device["model"]} '
                           f'w={device["width_um"]}u l={device["length_um"]}u')
    for index, (a, b, value) in enumerate(capacitors):
        circuit.append(f'Cremaining{index} {a} {b} {value:.17g} ic=0')
    circuit += ['Xdetector ' + ' '.join(blocks['detector'].values()) + ' PFD_RESET_B IHPPhaseDetector',
                'Xdivider ' + ' '.join(blocks['divider'].values()) + ' IHPClockDivider',
                '.ends PhysicalReferenceClock']
    # Reviewed native cells already have foundry ng/AD/AS/PD/PS records. Apply
    # finger sizing only to the surrounding C++-bound Xremaining devices.
    text = '\n'.join(circuit) + '\n'
    declarations = text[:text.index('.subckt PhysicalReferenceClock')]
    surrounding, audit['surrounding_geometry'] = size_mos_instances(text[len(declarations):])
    return declarations + surrounding, audit
