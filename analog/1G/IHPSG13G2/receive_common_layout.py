"""Literal physical realization of the selected shared receive reference IC."""
from collections import Counter, defaultdict
import json
import math
from pathlib import Path
import re

import klayout.db as db

from capacitor_bank_layout import append_mim_bank
from capacitor_layout import append_mim
from mos_layout import append_mos
from reference_clock_row import create_row
from reference_clock_placement import placement_order
from resistor_layout import append_rppd
from transmit_translator_layout import reinforce_source_contacts, reinforce_output_drain_contacts

PORTS = ['VDD', 'VSS', 'OUT']
PROBES = ['TARGET', 'DRIVE', 'BIAS_N', 'BIAS_P', 'TAIL_BIAS',
          'LEFT', 'RIGHT', 'TAIL', 'MIRROR', 'PMIRROR', 'POUTPUT']


def circuit(source, probes):
    text = Path(source).read_text()
    probes = json.loads(Path(probes).read_text())
    if ('module ReceiveCommonReference(' + ', '.join(PORTS) + ');' not in text or
            set(probes) != set(PROBES) or len(set(probes.values())) != len(PROBES)):
        raise ValueError('changed receive reference export interface')
    names = set(re.findall(r'\bcpphdl_n\d+\b', text)) | set(PORTS)
    if not set(probes.values()) <= names or set(probes.values()) & set(PORTS):
        raise ValueError('receive reference probe is absent or aliases a port')
    rename = {node: name for name, node in probes.items()} | {p: p for p in PORTS}
    node = lambda name: rename.get(name, name.replace('cpphdl_n', 'N'))
    records = re.findall(r'^\s*// device (\d+), IC [^,]+, ([^\n]+):\d+\n[ \t]*(I\([^\n]+)', text, re.M)
    if (len(records) != 39 or len({r[0] for r in records}) != 39 or
            len(re.findall(r'^\s*I\(', text, re.M)) != 39):
        raise ValueError('receive reference device ownership inventory changed')
    resistors, capacitors, transistors = [], [], []
    for identity, origin, law in records:
        match = re.fullmatch(r'I\((\w+), (\w+)\) <\+ V\((\w+), (\w+)\) / ([-+0-9.eE]+);', law)
        if match:
            a, b, va, vb, value = match.groups()
            if (a, b) != (va, vb):
                raise ValueError('incorrect resistor terminals')
            resistors.append((node(a), node(b), float(value)))
            continue
        match = re.fullmatch(r'I\((\w+), (\w+)\) <\+ ([-+0-9.eE]+) \* ddt\(V\((\w+), (\w+)\)\); '
                             r'// Bench initial voltage = 0.0 V\.', law)
        if match:
            a, b, value, va, vb = match.groups()
            if (a, b) != (va, vb):
                raise ValueError('incorrect storage terminals')
            capacitors.append((node(a), node(b), float(value)))
            continue
        match = re.fullmatch(r'I\((\w+), (\w+)\) <\+ cpphdl_channel\(V\((\w+), (\w+)\), '
                            r'V\((\w+), (\w+)\), ([-+0-9.eE]+), ([-+0-9.eE]+), ([-+0-9.eE]+)\);', law)
        if not match:
            raise ValueError('unbound receive reference law')
        a, b, gate, gb, da, db, beta, threshold, sign = match.groups()
        if (a, b, b) != (da, db, gb):
            raise ValueError('incorrect channel terminals')
        transistors.append((Path(origin).name, node(a), node(gate), node(b),
                            float(beta), float(threshold), float(sign)))
    # These widths are the reviewed lead_receive_common_process binding. Node
    # and owner checks disambiguate equal generic beta values in different ICs.
    expected = [('lead_receive_common_reference.hpp', 'TAIL_BIAS', 'TAIL_BIAS', 'VSS', .03, 1, 100),
        ('cascode_ota.hpp', 'LEFT', 'OUT', 'TAIL', .03, 1, 100),
        ('cascode_ota.hpp', 'RIGHT', 'TARGET', 'TAIL', .03, 1, 100),
        ('cascode_ota.hpp', 'MIRROR', 'BIAS_N', 'LEFT', .06, 1, 200),
        ('cascode_ota.hpp', 'DRIVE', 'BIAS_N', 'RIGHT', .06, 1, 200),
        ('cascode_ota.hpp', 'PMIRROR', 'MIRROR', 'VDD', .03, -1, 300),
        ('cascode_ota.hpp', 'POUTPUT', 'MIRROR', 'VDD', .03, -1, 300),
        ('cascode_ota.hpp', 'MIRROR', 'BIAS_P', 'PMIRROR', .06, -1, 600),
        ('cascode_ota.hpp', 'DRIVE', 'BIAS_P', 'POUTPUT', .06, -1, 600),
        ('cascode_ota.hpp', 'TAIL', 'TAIL_BIAS', 'VSS', .06, 1, 200),
        ('shunt_voltage_buffer.hpp', 'OUT', 'DRIVE', 'VSS', .03, 1, 150)]
    if len(transistors) != len(expected):
        raise ValueError('receive reference transistor inventory changed')
    devices = []
    for actual, want in zip(transistors, expected):
        origin, a, gate, b, beta, sign, width = want
        if actual[:4] != want[:4] or any(not math.isclose(x, y, rel_tol=1e-12)
                for x, y in zip(actual[4:], (beta, .7, sign))):
            raise ValueError('receive reference feedback or device binding changed')
        devices.append(dict(model='sg13_hv_' + ('nmos' if sign == 1 else 'pmos'),
            a=a, gate=gate, b=b, body='VSS' if sign == 1 else 'VDD', width_um=width, length_um=.45))
    branches = lambda values: Counter((tuple(sorted((a, b))), value) for a, b, value in values)
    leads = [b if a == 'DRIVE' else a for a, b, value in resistors if 'DRIVE' in (a, b) and value == 1000]
    if len(leads) != 1 or leads[0] in PORTS + PROBES:
        raise ValueError('missing independent lead-compensation node')
    lead = leads[0]
    expected_resistors = [('VDD', 'TARGET', 10000), ('TARGET', 'VSS', 12150),
        ('VDD', 'BIAS_N', 12000), ('BIAS_N', 'VSS', 21000), ('VDD', 'BIAS_P', 23000),
        ('BIAS_P', 'VSS', 43000), ('VDD', 'TAIL_BIAS', 2500), ('VDD', 'OUT', 1000), ('DRIVE', lead, 1000)]
    expected_caps = [(name, 'VSS', 10e-12) for name in ('TARGET', 'BIAS_N', 'BIAS_P', 'TAIL_BIAS')]
    expected_caps += [('DRIVE', 'VSS', .25e-12), ('LEFT', 'VSS', 40e-15), ('RIGHT', 'VSS', 40e-15),
        ('TAIL', 'VSS', 40e-15), ('PMIRROR', 'VDD', 40e-15), ('POUTPUT', 'VDD', 40e-15),
        ('MIRROR', 'VSS', 40e-15), ('OUT', 'TAIL', 40e-15), ('TARGET', 'TAIL', 40e-15),
        ('OUT', 'LEFT', 10e-15), ('TARGET', 'RIGHT', 10e-15), ('OUT', 'VSS', 80e-12),
        ('DRIVE', 'VSS', 80e-15), ('DRIVE', 'OUT', 20e-15), (lead, 'OUT', 2e-12)]
    if branches(resistors) != branches(expected_resistors) or branches(capacitors) != branches(expected_caps):
        raise ValueError('receive reference bias or compensation branches changed')
    return devices, resistors, capacitors


def create_receive_common_reference(pdk, source, probes):
    devices, resistors, capacitors = circuit(source, probes)
    merged = defaultdict(list)
    for a, b, value in capacitors:
        merged[tuple(sorted((a, b)))].append(value)
    def parts(layout):
        result = []
        for index, device in enumerate(devices):
            # The pinned PCell permits at most 100 fingers. Larger devices
            # use explicitly connected equal guarded units, preserving W/L.
            units = math.ceil(device['width_um'] / 500)
            width = device['width_um'] / units
            for unit in range(units):
                name = 'ReceiveCommonMOS' + str(index) + (f'_{unit}' if units > 1 else '')
                cell, info = append_mos(layout, pdk, name, width, .45, int(width / 5),
                    'n' if device['model'].endswith('nmos') else 'p', 'hv')
                reinforce_source_contacts(layout, cell, info)
                reinforce_output_drain_contacts(layout, cell, info)
                info['logical_device_index'] = index
                result.append((cell, info, dict(D=device['a'], G=device['gate'], S=device['b'], B=device['body']), 'mos'))
        for index, (a, b, value) in enumerate(resistors):
            cell, info = append_rppd(layout, pdk, 'ReceiveCommonResistance' + str(index), value,
                                    width_um=2 if value <= 2500 else .5)
            result.append((cell, info, dict(PLUS=a, MINUS=b, SUB='VSS'), 'resistor'))
        for index, ((a, b), values) in enumerate(sorted(merged.items())):
            value = math.fsum(values)
            make = append_mim_bank if value > 5e-12 else append_mim
            cell, info = make(layout, pdk, 'ReceiveCommonStorage' + str(index), value)
            result.append((cell, info, dict(PLUS=a, MINUS=b), 'capacitor'))
        return result
    def order(parts, dbu):
        return placement_order(parts, dbu, power_nets=('VDD', 'VSS'),
                               net_weights=dict(OUT=6, DRIVE=6, TAIL=4, MIRROR=4))
    layout, top, info = create_row('ReceiveCommonReference', PORTS, parts, order_parts=order,
        priority_nets=('OUT', 'DRIVE'), signal_width_um=2)
    add_supply_routes(layout, top, info)
    info['export_inventory'] = dict(mos=len(devices), resistors=len(resistors),
        capacitor_contributions=len(capacitors), merged_storage_branches=len(merged))
    return layout, top, info


def add_supply_routes(layout, top, info):
    def box(layer, x0, y0, x1, y1):
        top.shapes(layout.layer(layer, 0)).insert(db.Box(*(round(v / layout.dbu)
            for v in (x0, y0, x1, y1))))
    columns = []
    for part in info['components']:
        if part['kind'] != 'mos' or part['connections']['S'] not in ('VDD', 'VSS'):
            continue
        net = part['connections']['S']
        dx, dy = part['origin_um']
        extent = part['geometry']['supply_source_extent_um']
        x = dx + sum(extent) / 2
        sy = dy + part['geometry']['terminals_um']['S'][1]
        by = info['terminals'][net]['y_um']
        box(30, x - 1.67, sy - .62, x + 1.67, sy + .62)
        box(50, x - 1.67, by - .62, x + 1.67, sy + .62)
        box(67, x - 1.67, by - .62, x + 1.67, by + .62)
        for layer, y in ((49, sy), (66, by)):
            for ix in range(8):
                for iy in range(3):
                    vx, vy = x + (ix - 3.5) * .42, y + (iy - 1) * .42
                    box(layer, vx - .095, vy - .095, vx + .095, vy + .095)
        columns.append(dict(component=part['name'], net=net, x_um=x, source_y_um=sy, bus_y_um=by))
    for net in ('VDD', 'VSS'):
        xs = [e['location_um'][0] for e in info['escapes'] if e['net'] == net]
        xs += [c['x_um'] for c in columns if c['net'] == net]
        left, right, y = min(xs) - .2, max(xs) + .2, info['terminals'][net]['y_um']
        box(67, left, y - 2.5, right, y + 2.5)
        box(126, left, y - 1.9, right, y + 1.9)
        first = math.ceil((left + .65) / .84) * .84
        for index in range(math.floor((right - .65 - first) / .84) + 1):
            x = first + index * .84
            for row in range(4):
                cy = y + (row - 1.5) * .84
                box(125, x - .21, cy - .21, x + .21, cy + .21)
    info['direct_supply_columns'] = columns
    info.update(width_um=top.bbox().width() * layout.dbu, height_um=top.bbox().height() * layout.dbu,
                bounding_area_um2=top.bbox().area() * layout.dbu ** 2)
