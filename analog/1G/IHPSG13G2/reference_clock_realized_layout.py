"""Compose the exported PLL around native detector/divider physical ICs."""
from collections import Counter, defaultdict
import json
import math
from pathlib import Path
import re

from analog_row_fold import fold_row
from antenna_layout import append_antenna
from capacitor_bank_layout import append_mim_bank
from capacitor_layout import append_mim
from capacitor_series_layout import append_mim_series
from clock_macro_layout import append_clock_macro
from mos_layout import append_mos
from oscillator_passive_layout import append_one_ff
from reference_clock_layout import extend_logic_supply
from reference_clock_layout_process import PORTS
from reference_clock_placement import placement_order
from reference_clock_realization import partition
from reference_clock_row import create_row
from resistor_layout import append_rppd


def unique_definitions(schematic):
    """Share identical foundry cell definitions across the two native ICs."""
    pattern = re.compile(r'^\.subckt\s+(\S+)[^\n]*\n.*?^\.ends[^\n]*(?:\n|$)', re.M | re.I | re.S)
    definitions, output, cursor = {}, [], 0
    for match in pattern.finditer(schematic):
        if any(line.strip() and not line.lstrip().startswith('*')
               for line in schematic[cursor:match.start()].splitlines()):
            raise ValueError('unscoped physical clock schematic statement')
        name, body = match[1].upper(), match[0].strip()
        if name in definitions and definitions[name] != body:
            raise ValueError('conflicting native clock library definition: ' + name)
        if name not in definitions:
            definitions[name] = body
            output.append(body)
        cursor = match.end()
    if not definitions or schematic[cursor:].strip():
        raise ValueError('incomplete physical clock schematic definitions')
    return '\n\n'.join(output) + '\n'


def create_realized_reference_clock(pdk, source, realizations):
    devices, storage, blocks, audit = partition(Path(source).read_text(),
                                               json.loads(Path(realizations).read_text()))
    capacitors = defaultdict(float)
    for a, b, value in storage:
        capacitors[tuple(sorted((a, b)))] += value
    ring = [(i, device) for i, device in enumerate(devices)
            if device.get('role', '').startswith('ring_')]
    control = [net for net, count in Counter(d['gate'] for _, d in ring
               if d['role'] == 'ring_n').items() if count == 4]
    mirrors = {d['a'] for _, d in ring if d['a'] == d['gate']}
    if len(ring) != 14 or len(control) != 1 or len(mirrors) != 1:
        raise ValueError('unreviewed realized PLL oscillator')
    power = {'VDD', 'VSS', 'VLOGIC'}
    sensitive = sorted({d[k] for _, d in ring for k in ('a', 'b', 'gate')}
                       - power - set(control) - mirrors)
    # Tuning and mirror-bias storage must remain with the oscillator too.
    # Putting these capacitors in the support row creates long high-impedance
    # connections through the switching-clock routing.
    oscillator_nets = set(sensitive) | set(control) | mirrors
    ring_names = {'RealizedMOS' + str(index) for index, _ in ring}

    def parts(layout):
        result = []
        for index, device in enumerate(devices):
            if device['kind'] == 'R':
                cell, info = append_rppd(layout, pdk, 'RealizedResistance' + str(index),
                    device['value'], width_um=4 if device['value'] <= 1000 else .5)
                connections, kind = dict(PLUS=device['a'], MINUS=device['b'], SUB='VSS'), 'resistor'
            else:
                width = device['width_um']
                cell, info = append_mos(layout, pdk, 'RealizedMOS' + str(index), width,
                    device['length_um'], math.ceil(width / 10),
                    'p' if device['model'].endswith('pmos') else 'n',
                    'hv' if '_hv_' in device['model'] else 'lv')
                connections = dict(D=device['a'], G=device['gate'], S=device['b'], B=device['body'])
                kind = 'mos'
            result.append((cell, info, connections, kind))
        for index, ((a, b), value) in enumerate(sorted(capacitors.items())):
            make = (append_one_ff if value < 2e-15 else append_mim_series if value < 3e-15
                    else append_mim_bank if value > 8e-12 else append_mim)
            cell, info = make(layout, pdk, 'RealizedStorage' + str(index), value)
            result.append((cell, info, dict(PLUS=a, MINUS=b), 'capacitor'))
        for kind in ('detector', 'divider'):
            cell, info = append_clock_macro(layout, pdk, 'Realized' + kind.title(), kind)
            connections = dict(blocks[kind])
            if kind == 'detector':
                connections['RESET_B'] = 'PFD_RESET_B'
            if list(connections) != info['ports']:
                raise ValueError('native clock physical port order differs from C++ binding')
            result.append((cell, info, connections, 'macro'))
        cell, info = append_antenna(layout, pdk, 'RealizedControlProtection')
        result.append((cell, info, dict(PLUS=control[0], MINUS='VLOGIC'), 'diode'))
        return result

    def order_parts(parts, dbu):
        oscillator = [i for i, (cell, _, pins, _) in enumerate(parts)
                      if cell.name in ring_names or set(pins.values()) & oscillator_nets]
        support = [i for i in range(len(parts)) if i not in oscillator]
        order, details, starts = [], {}, []
        for name, group in [('oscillator', oscillator), ('support', support)]:
            width = sum(math.ceil((parts[i][0].bbox().width() * dbu + 10) * 100) / 100
                        for i in group)
            if not group or width > 1800:
                raise ValueError('realized clock group does not fit one row: ' + name)
            outside = set().union(*(set(parts[i][2].values()) for i in range(len(parts))
                                   if i not in group)) - power
            local, detail = placement_order([parts[i] for i in group], dbu,
                power_nets=power, net_weights={net: 20 for net in oscillator_nets},
                row_width_um=1800, left_anchored_nets=outside)
            chosen = [group[i] for i in local]
            starts.append(parts[chosen[0]][0].name)
            order += chosen
            details[name] = dict(width_um=width, optimization=detail,
                                component_order=[parts[i][0].name for i in chosen])
        return order, dict(strategy='preserve native ICs and oscillator feedback within rows',
            row_width_um=1800, row_start_components=starts, groups=details,
            component_order=[parts[i][0].name for i in order])

    # The foundry native-cell models connect NMOS bodies directly to VSS.
    # Their substrate contacts share the wafer with the custom devices; retain
    # the custom tap devices, but bind their wafer side to that same net.
    if any(blocks[kind]['VSS'] != 'VSS' for kind in ('detector', 'divider')):
        raise ValueError('native clock macros require a common ground substrate')
    layout, top, info = create_row('RealizedReferenceClock', PORTS, parts,
        order_parts=order_parts, priority_nets=sorted(oscillator_nets), substrate_net='VSS')
    info['schematic'] = unique_definitions(info['schematic'])
    info['realization_boundary'] = audit
    info['merged_outer_storage_branches'] = len(capacitors)
    info['protected_nets'] = control
    extend_logic_supply(layout, top, info)
    layout, top, info = fold_row(layout, top, info)
    components = {part['name']: part for part in info['components']}
    for group in info['placement']['groups'].values():
        bottoms = {round(components[name]['origin_um'][1] +
                   layout.cell(name).bbox().bottom * layout.dbu, 3)
                   for name in group['component_order']}
        if len(bottoms) != 1:
            raise ValueError('folding split a realized clock feedback group')
    info.update(width_um=top.bbox().width() * layout.dbu,
                height_um=top.bbox().height() * layout.dbu,
                bounding_area_um2=top.bbox().area() * layout.dbu ** 2)
    return layout, top, info
