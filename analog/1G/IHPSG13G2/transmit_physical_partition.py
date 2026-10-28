"""Validate lossless partitioning of the actual C++ transmitter for physical assembly."""
from collections import Counter
import json
from pathlib import Path
import re


def contributions(path, model, ports):
    text = Path(path).read_text()
    interface = re.findall(r'^module (\w+)\(([^\n]+)\);$', text, re.M)
    if interface != [(model, ', '.join(ports))]:
        raise ValueError('transmit partition export interface differs')
    records = re.findall(r'^\s*// device (\d+), IC [^\n]+\n[ \t]*(I\([^\n]+)$', text, re.M)
    if ([int(i) for i, _ in records] != list(range(len(records))) or
            len(records) != len(re.findall(r'^\s*I\(', text, re.M))):
        raise ValueError('incomplete transmit contribution inventory')

    def node(match):
        name = match[0]
        if name in ports:
            return 'global_' + str(ports[name])
        if name.startswith('cpphdl_n') and name[8:].isdecimal():
            return 'global_' + name[8:]
        return name
    return [re.sub(r'\b\w+\b', node, law) for _, law in records]


def validate_partition(directory):
    directory = Path(directory)
    manifest = json.loads((directory / 'partition.json').read_text())
    ports = manifest['ports']
    expected_ports = ['VDD', 'VSS', 'VLOGIC', 'P', 'N']
    expected_ports += [prefix + str(bit) for prefix in ('MAIN_P', 'MAIN_N', 'HISTORY_P', 'HISTORY_N')
                       for bit in range(4)]
    expected_ports += ['TRIM_N' + str(bit) for bit in range(6)]
    if (list(ports) != expected_ports or len(set(ports.values())) != 27 or
            any(type(n) is not int or n < 0 for n in ports.values())):
        raise ValueError('changed complete transmitter ports')
    counts = {**{f'translator_{i}': 36 for i in range(22)}, 'translation_bias': 5, 'current_bias': 16,
              **{f'{bank}_{i}': 4 for bank in ('main', 'history') for i in range(4)}, 'remaining_passives': 86}
    entries = manifest['blocks']
    if [b['name'] for b in entries] != list(counts) or manifest['elements'] != 931:
        raise ValueError('changed complete transmitter block inventory')
    whole = contributions(directory / 'transmitter.va', 'OneGPhysicalTransmit', ports)
    if len(whole) != 931:
        raise ValueError('whole transmitter has a changed contribution count')
    used = []
    blocks = {}
    for block in entries:
        name, bindings, indices = block['name'], block['ports'], block['elements']
        if (len(indices) != counts[name] or indices != sorted(set(indices)) or
                any(type(i) is not int or not 0 <= i < len(whole) for i in indices) or
                len(bindings) != len(set(bindings.values()))):
            raise ValueError('aliased or incomplete transmitter block: ' + name)
        local = contributions(directory / (name + '.va'), block['model'], bindings)
        if local != [whole[i] for i in indices]:
            raise ValueError('transmitter partition changes a law or connection: ' + name)
        used.extend(indices)
        blocks[name] = bindings
    if sorted(used) != list(range(931)):
        raise ValueError('transmitter contribution omitted or claimed twice')

    def same(actual, expected, context):
        if any(actual.get(key) != value for key, value in expected.items()):
            raise ValueError('transmitter physical boundary differs: ' + context)

    replica, bias = blocks['translation_bias'], blocks['current_bias']
    same(replica, {p: ports[p] for p in ('VDD', 'VSS', 'VLOGIC')}, 'replica supplies')
    same(bias, {p: ports[p] for p in ('VDD', 'VSS')}, 'current-bias supplies')
    outputs = []
    for index in range(22):
        gate = blocks[f'translator_{index}']
        name = (('MAIN_P', 'MAIN_N', 'HISTORY_P', 'HISTORY_N')[index // 4] + str(index % 4)
                if index < 16 else 'TRIM_N' + str(index - 16))
        same(gate, {**{p: ports[p] for p in ('VDD', 'VSS', 'VLOGIC')},
                    'BIAS': replica['BIAS'], 'A': ports[name]}, name)
        outputs.append(gate['Y'])
        if index >= 16:
            same(bias, {'TRIM_N' + str(index - 16): gate['Y']}, 'translated trim')
    if len(set(outputs)) != 22 or set(outputs) & set(ports.values()):
        raise ValueError('transmitter gate commands bypass translation or share outputs')
    anchors = []
    for bank_index, bank in enumerate(('main', 'history')):
        for bit in range(4):
            cell = blocks[f'{bank}_{bit}']
            same(cell, {**{p: ports[p] for p in ('P', 'N', 'VSS')}, 'BIAS': bias['BIAS'],
                        'CASCODE': bias['CASCODE'], 'GP': outputs[bank_index * 8 + bit],
                        'GN': outputs[bank_index * 8 + 4 + bit]}, bank + str(bit))
            anchors += [cell['TAIL'], cell['LOWER']]
    if (len(set(anchors)) != 16 or set(anchors) & (set(ports.values()) | set(outputs) | set(bias.values())) or
            not set(anchors) <= set(blocks['remaining_passives'].values())):
        raise ValueError('current-cell storage anchors are missing or aliased')
    passive_laws = contributions(directory / 'remaining_passives.va', 'OneGTransmitPassives',
                                 blocks['remaining_passives'])
    kinds = Counter('capacitor' if 'ddt(' in law else 'resistor' if ' / ' in law else 'other'
                    for law in passive_laws)
    if kinds != dict(capacitor=84, resistor=2):
        raise ValueError('remaining transmitter storage/pullups changed')
    return dict(status='pass', elements=931, blocks=33, translators=22, current_cells=8,
        remaining_capacitors=84, remaining_resistors=2, internal_storage_anchors=16,
        scope='exact C++ contribution and boundary coverage; no loaded physical TX or layout qualification')
