"""Keep the transmitter's declared storage beside its physical current cells."""
import json
import math
from pathlib import Path
import re

from capacitor_bank_layout import append_mim_bank
from capacitor_layout import append_mim
from reference_clock_row import create_row
from transmit_current_binding import PORTS
from transmit_physical_partition import contributions, validate_partition
from transmit_storage_substrate import add_storage_substrate_tap


def storage_partition(directory):
    """Assign all 84 retained capacitors without relocating local bias charge.

    Even the eight BIAS-to-VSS gate-storage contributions belong beside their
    current cells. Combining them with the remote 400 pF bias filter would
    change their interaction with the routed bias wire.
    """
    directory = Path(directory)
    validate_partition(directory)
    manifest = json.loads((directory / 'partition.json').read_text())
    blocks = {b['name']: b for b in manifest['blocks']}
    remaining = blocks['remaining_passives']
    laws = contributions(directory / 'remaining_passives.va', remaining['model'], remaining['ports'])
    capacitor = re.compile(r'I\(global_(\d+), global_(\d+)\) <\+ ([-+0-9.eE]+) \* '
        r'ddt\(V\(global_(\d+), global_(\d+)\)\); // Bench initial voltage = 0.0 V\.')
    resistor = re.compile(r'I\(global_(\d+), global_(\d+)\) <\+ '
        r'V\(global_(\d+), global_(\d+)\) / ([-+0-9.eE]+);')
    capacitors, resistors = [], []
    for index, law in enumerate(laws):
        match = capacitor.fullmatch(law)
        if match:
            a, b, value, va, vb = match.groups()
            value = float(value)
            if (a, b) != (va, vb) or not math.isfinite(value) or value <= 0 or a == b:
                raise ValueError('invalid retained transmitter capacitor')
            capacitors.append(dict(element=remaining['elements'][index],
                                   nodes=[int(a), int(b)], capacitance_F=value))
            continue
        match = resistor.fullmatch(law)
        if not match:
            raise ValueError('unsupported retained transmitter passive')
        a, b, va, vb, value = match.groups()
        if (a, b) != (va, vb) or float(value) != 50 or a == b:
            raise ValueError('changed physical transmitter pullup')
        resistors.append(dict(element=remaining['elements'][index], nodes=[int(a), int(b)], resistance_Ohm=50.))

    # The C++ declaration order records locality even where two branches share
    # electrical nodes. Preserve that identity rather than guessing by value.
    if len(capacitors) != 84 or len(resistors) != 2:
        raise ValueError('changed transmitter passive count')
    ports = dict(manifest['ports'])
    ports.update({p: blocks['current_bias']['ports'][p] for p in ('BIAS', 'CASCODE')})
    shared = [('BIAS', 'VSS', 400e-12), ('CASCODE', 'VSS', 50e-12),
              ('P', 'VDD', .5e-12), ('N', 'VDD', .5e-12)]
    groups = {}

    def group(name, bindings, records, expected):
        if len(records) != len(expected):
            raise ValueError('incomplete transmitter storage group')
        result = []
        for record, (a, b, value) in zip(records, expected):
            if (record['nodes'] != [bindings[a], bindings[b]] or
                    not math.isclose(record['capacitance_F'], value, rel_tol=1e-12)):
                raise ValueError('changed transmitter storage value, connection or locality: ' + name)
            result.append({**record, 'terminals': [a, b]})
        groups[name] = dict(ports=bindings, capacitors=result)

    group('shared', {p: ports[p] for p in ('VDD', 'VSS', 'P', 'N', 'BIAS', 'CASCODE')},
          capacitors[:4], shared)
    for bank_index, bank in enumerate(('main', 'history')):
        steering, sink, cascode = (4., 1.5, 3.) if bank == 'main' else (2., .5, 1.)
        expected = [('TAIL', 'VSS', 110e-15), ('LOWER', 'VSS', 30e-15 * cascode),
                    ('GP', 'TAIL', 180e-15 * steering), ('GN', 'TAIL', 180e-15 * steering),
                    ('GP', 'P', 45e-15 * steering), ('GN', 'N', 45e-15 * steering),
                    ('BIAS', 'VSS', 540e-15 * sink), ('BIAS', 'LOWER', 135e-15 * sink),
                    ('CASCODE', 'LOWER', .9e-12 * cascode), ('CASCODE', 'TAIL', .225e-12 * cascode)]
        for index in range(4):
            name = f'{bank}_{index}'
            start = 4 + (bank_index * 4 + index) * 10
            group(name, blocks[name]['ports'], capacitors[start:start + 10], expected)
    expected_pullups = [[ports['VDD'], ports[p]] for p in ('P', 'N')]
    if [r['nodes'] for r in resistors] != expected_pullups:
        raise ValueError('changed transmitter line pullup connections')
    return dict(groups=groups, pullups=resistors, capacitors=84,
        local_capacitors=80, shared_capacitors=4,
        scope='declared storage retained by component locality; routed electrical qualification required')


def create_current_storage(pdk, directory, history=False):
    """Create a routed ten-capacitor bank for one main/history current cell."""
    plan = storage_partition(directory)
    group = plan['groups']['history_0' if history else 'main_0']
    name = 'TransmitHistoryStorage' if history else 'TransmitMainStorage'
    return create_storage(pdk, name, PORTS, group)


def create_output_storage(pdk, directory):
    """Keep the two line capacitors; bias storage uses qualified MOS macros.

    A 400 pF MIM filter alone exceeds the PDK's recommended whole-chip MIM
    area. The existing dedicated MOSCAP and HV gate-storage realizations own
    the first two shared contributions, with their distinct charge models.
    """
    group = storage_partition(directory)['groups']['shared']
    outputs = dict(ports={p: group['ports'][p] for p in ('VDD', 'VSS', 'P', 'N')},
                   capacitors=group['capacitors'][2:])
    return create_storage(pdk, 'TransmitOutputStorage', list(outputs['ports']), outputs)


def create_storage(pdk, name, ports, group):
    def parts(layout):
        result = []
        for index, capacitor in enumerate(group['capacitors']):
            value = capacitor['capacitance_F']
            make = append_mim_bank if value > 8e-12 else append_mim
            cell, info = make(layout, pdk, name + 'C' + str(index), value)
            a, b = capacitor['terminals']
            result.append((cell, info, dict(PLUS=a, MINUS=b), 'capacitor'))
        return result

    connected = {p for capacitor in group['capacitors'] for p in capacitor['terminals']}
    row_ports = [p for p in ports if p in connected]
    if set(ports) - connected - {'VSS'}:
        raise ValueError('unconnected storage signal port')
    layout, top, info = create_row(name, row_ports, parts,
        priority_nets=('TAIL', 'LOWER', 'BIAS', 'CASCODE'), via_columns=2, signal_width_um=2)
    info.update(storage_group=group, intended_storage_branches=len(group['capacitors']))
    add_storage_substrate_tap(layout, top, info, pdk)
    return layout, top, info
