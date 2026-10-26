"""Connected PLL/translator geometry from the exported reference-clock IC."""
from collections import Counter, defaultdict
import math
from pathlib import Path
import re

import klayout.db as db

from reference_clock_row import create_row
from analog_row_fold import fold_row
from antenna_layout import append_antenna
from capacitor_bank_layout import append_mim_bank
from capacitor_layout import append_mim
from capacitor_series_layout import append_mim_series
from core_driver_process import contributions
from mos_layout import append_mos
from oscillator_passive_layout import append_one_ff
from reference_clock_process import BINDINGS
from resistor_layout import append_rppd
from reference_clock_placement import placement_order
from reference_clock_hierarchy import physical_groups

PORTS = ['VDD', 'VSS', 'VLOGIC', 'REF', 'CLEAR', 'OUT', 'OUT_N']


def circuit(source):
    text = Path(source).read_text()
    ports = ['Vdd', 'Vss', 'Vlogic', *PORTS[3:]]
    if 'module OneGIHPReferenceClockGenerator(' + ', '.join(ports) + ');' not in text:
        raise ValueError('requires the exported reference-clock IC')
    names = ['Vss', 'Vdd', 'Vlogic', *PORTS[3:]] + sorted(set(re.findall(r'\bcpphdl_n\d+\b', text)))
    mapping = {name: 'n' + str(i) for i, name in enumerate(names)}
    text = re.sub(r'\b(?:' + '|'.join(names) + r')\b', lambda m: mapping[m[0]], text)
    capacitor = re.compile(r'^\s*I\((n\d+), (n\d+)\) <\+ ([-+0-9.eE]+) \* '
                           r'ddt\(V\((n\d+), (n\d+)\)\); // Bench initial voltage = 0.0 V\.$', re.M)
    capacitors = defaultdict(float)
    for match in capacitor.finditer(text):
        a, b, value, va, vb = match.groups()
        if (a, b) != (va, vb) or a == b or not math.isfinite(float(value)) or float(value) <= 0:
            raise ValueError('invalid reference-clock storage law')
        capacitors[a, b] += float(value)
    devices, roles = contributions(capacitor.sub('', text), BINDINGS)
    if roles != Counter({row[0]: row[-1] for row in BINDINGS}):
        raise ValueError('changed reference-clock transistor inventory')
    resistances = Counter(d['value'] for d in devices if d['kind'] == 'R')
    if resistances != Counter({10000.: 4, 1000.: 2, 2000.: 1, 200.: 1, 800.: 1}):
        raise ValueError('unreviewed reference-clock resistance inventory')
    rename = {mapping[name]: name.upper() for name in names[:len(PORTS)]}
    rename['0'] = 'VSS'

    def net(name):
        return rename.get(name, name.upper())

    for device in devices:
        for key in ('a', 'b', 'gate', 'body'):
            if key in device:
                device[key] = net(device[key])
    return devices, [(net(a), net(b), value) for (a, b), value in sorted(capacitors.items())]


def extend_logic_supply(layout, top, info):
    """Give the third rail a 5 um bus below the existing signal tracks."""
    port = info['terminals']['VLOGIC']
    old_y = round(port['y_um'] / layout.dbu)
    pins = [p['location_um'] for p in info['escapes'] if p['net'] == 'VLOGIC']
    if len(pins) < 2 or port['layer'] != 67:
        raise ValueError('missing logic supply escapes')
    # Remove only this rail's old horizontal bus, landing metal and labels.
    # Its dedicated Metal4 columns are retained and extended to the new rail.
    removed = Counter()
    for layer, datatype in ((67, 0), (66, 0), (67, 2), (67, 25)):
        for shape in list(top.each_shape(layout.layer(layer, datatype))):
            if shape.is_text():
                selected = shape.text.string == 'VLOGIC'
            else:
                selected = shape.bbox().center().y == old_y
            if selected:
                removed[layer, datatype] += 1
                shape.delete()
    if removed[67, 25] != 1 or removed[67, 2] != 1 or removed[66, 0] != len(pins):
        raise ValueError('ambiguous logic supply route')
    y = top.bbox().bottom * layout.dbu - 4

    def box(layer, x0, y0, x1, y1):
        top.shapes(layout.layer(layer, 0)).insert(db.Box(*(round(v / layout.dbu)
            for v in (x0, y0, x1, y1))))

    xs = [p[0] for p in pins]
    box(67, min(xs) - .2, y - 2.5, max(xs) + .2, y + 2.5)
    for x, endpoint in pins:
        box(50, x - .2, y - .2, x + .2, endpoint + .2)
        box(66, x - .095, y - .095, x + .095, y + .095)
    x = min(xs)
    top.shapes(layout.layer(67, 2)).insert(db.DBox(x - .16, y - .16, x + .16, y + .16))
    top.shapes(layout.layer(67, 25)).insert(db.DText('VLOGIC', db.DTrans(x, y)))
    info['terminals']['VLOGIC'] = dict(layer=67, x_um=x, y_um=y)
    info['logic_supply_bus'] = dict(width_um=5, y_um=y, connected_columns=len(pins))


def create_reference_clock(pdk, source, hierarchy):
    hierarchy_parts = physical_groups(Path(source).read_text(), hierarchy)
    devices, capacitors = circuit(source)
    control_gates = Counter(d['gate'] for d in devices if d.get('role') == 'ring_n')
    control = [net for net, count in control_gates.items() if count == 4]
    reset = {d['gate'] for d in devices if d.get('role') == 'clear_n' and d['gate'] != 'CLEAR'}
    if len(control) != 1 or len(reset) != 1:
        raise ValueError('ambiguous PLL control or common detector reset')
    protected = ['REF', control[0], next(iter(reset))]
    # The oscillator's high-impedance switching nodes must stay local. The
    # previous unconstrained row optimization scattered these devices over
    # three folded rows and the extracted PLL could not reach 250 MHz.
    ring = [(i, d) for i, d in enumerate(devices) if d.get('role', '').startswith('ring_')]
    mirrors = [d['a'] for _, d in ring if d['a'] == d['gate']]
    if len(ring) != 14 or len(mirrors) != 1:
        raise ValueError('unreviewed balanced oscillator topology')
    power = {'VDD', 'VSS', 'VLOGIC'}
    sensitive = sorted({d[k] for _, d in ring for k in ('a', 'b', 'gate')}
                       - power - {control[0], mirrors[0]})
    ring_names = {'ReferenceMOS' + str(i) for i, _ in ring}

    def order_parts(parts, dbu):
        by_name = {cell.name: i for i, (cell, _, _, _) in enumerate(parts)}
        owned = [name for group in hierarchy_parts.values() for name in group]
        if len(owned) != len(set(owned)) or not set(owned) <= set(by_name):
            raise ValueError('ambiguous or missing physical hierarchy instance')
        groups = {name: [by_name[cell] for cell in cells]
                  for name, cells in hierarchy_parts.items()}
        support = [i for i, (cell, _, _, _) in enumerate(parts) if cell.name not in owned]
        selected = [i for i in support if parts[i][0].name in ring_names or
                    set(parts[i][2].values()) & set(sensitive)]
        rest = [i for i in support if i not in selected]
        groups = {'oscillator': selected, **groups, 'support': rest}
        result, details, starts = [], {}, []
        for name, group in groups.items():
            # Reserve one row per DFF. A row cut through master/slave feedback
            # adds two long buses and a spine to a timing-critical latch path.
            width = sum(math.ceil((parts[i][0].bbox().width() * dbu + 10) * 100) / 100
                        for i in group)
            if width > 1800:
                raise ValueError('clock hierarchy group exceeds one physical row: ' + name)
            outside = set().union(*(set(parts[i][2].values()) for i in range(len(parts))
                                   if i not in group)) - power
            local_order, local_details = [], []
            cursor = 0
            subdivisions = (group,)
            for section in subdivisions:
                if not section:
                    continue
                order, detail = placement_order([parts[i] for i in section], dbu,
                    power_nets=power, net_weights={net: 20 for net in sensitive},
                    row_width_um=1800, prefix_width_um=cursor,
                    left_anchored_nets=outside)
                local_order.extend(section[i] for i in order)
                local_details.append(detail)
                cursor += sum(math.ceil((parts[i][0].bbox().width() * dbu + 10) * 100) / 100
                              for i in section)
            starts.append(parts[local_order[0]][0].name)
            result.extend(local_order)
            details[name] = dict(width_um=width, optimizations=local_details,
                component_order=[parts[i][0].name for i in local_order])
        return result, dict(strategy='C++ IC flip-flops retained within physical rows',
            sensitive_nets=sensitive, groups=details, row_width_um=1800,
            row_start_components=starts,
            component_order=[parts[i][0].name for i in result])

    def parts(layout):
        result = []
        for index, device in enumerate(devices):
            if device['kind'] == 'R':
                # Bias/reference branches have different current and minimum
                # contact-length requirements from the high-value loop filter.
                width = 4 if device['value'] <= 1000 else .5
                cell, info = append_rppd(layout, pdk, 'ReferenceResistance' + str(index),
                                        device['value'], width_um=width)
                connections = dict(PLUS=device['a'], MINUS=device['b'], SUB='VSS')
                kind = 'resistor'
            else:
                width = device['width_um']
                cell, info = append_mos(layout, pdk, 'ReferenceMOS' + str(index), width,
                    device['length_um'], math.ceil(width / 10),
                    'p' if device['model'].endswith('pmos') else 'n',
                    'hv' if '_hv_' in device['model'] else 'lv')
                connections = dict(D=device['a'], G=device['gate'], S=device['b'], B=device['body'])
                kind = 'mos'
            result.append((cell, info, connections, kind))
        for index, (a, b, value) in enumerate(capacitors):
            make = append_one_ff if value < 2e-15 else append_mim_series if value < 3e-15 else \
                   append_mim_bank if value > 8e-12 else append_mim
            cell, info = make(layout, pdk, 'ReferenceStorage' + str(index), value)
            result.append((cell, info, dict(PLUS=a, MINUS=b), 'capacitor'))
        # The complete antenna deck identified the reference input, weak
        # tuning node and resistor-isolated detector reset. Preserve each
        # added junction in the physical schematic and subsequent PEX.
        for index, net in enumerate(protected):
            cell, info = append_antenna(layout, pdk, 'ReferenceProtection' + str(index))
            result.append((cell, info, dict(PLUS=net, MINUS='VLOGIC'), 'diode'))
        return result

    layout, top, info = create_row('ReferenceClockGenerator', PORTS, parts,
                                 order_parts=order_parts, priority_nets=sensitive)
    # LV and HV NMOS share the p-substrate. Preserve each real guard contact;
    # connect its substrate side to the same internal net as the LV devices.
    shared = []
    for component in info['components']:
        if component['kind'] == 'mos' and component['geometry']['model'] == 'sg13_hv_nmos':
            name = component['name']
            original = component['geometry']['schematic']
            definition = re.sub(r'\bbody\b', 'SUB', original.replace(' D G S B\n', ' D G S B SUB\n'))
            if info['schematic'].count(original) != 1:
                raise ValueError('ambiguous HV substrate definition')
            info['schematic'] = info['schematic'].replace(original, definition)
            info['schematic'], count = re.subn(r'^(X\d+ .*) ' + re.escape(name) + r'$',
                r'\1 PSUB ' + name, info['schematic'], flags=re.M)
            if count != 1:
                raise ValueError('ambiguous HV substrate instance')
            shared.append(name)
    info['common_hv_substrate'] = shared
    info['protected_nets'] = protected
    extend_logic_supply(layout, top, info)
    info.update(width_um=top.bbox().width() * layout.dbu,
        height_um=top.bbox().height() * layout.dbu,
        bounding_area_um2=top.bbox().area() * layout.dbu ** 2,
        intended_device_inventory=dict(MOS=230, resistance_branches=9, storage_branches=len(capacitors)))
    layout, top, info = fold_row(layout, top, info)
    # Check actual instance coordinates after folding, not just the optimizer's
    # intent. The bounding-box lower edges all start 4 um above their row.
    components = {part['name']: part for part in info['components']}
    rows = {}
    for name, group in info['placement']['groups'].items():
        bottoms = {round(components[part]['origin_um'][1] +
                         layout.cell(part).bbox().bottom * layout.dbu, 3)
                   for part in group['component_order']}
        if len(bottoms) != 1:
            raise ValueError('folding split a reference-clock hierarchy group: ' + name)
        rows[name] = next(iter(bottoms))
    info['hierarchy_rows_bottom_um'] = rows
    return layout, top, info
