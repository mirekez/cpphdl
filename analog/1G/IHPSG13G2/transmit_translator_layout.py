"""Physical clock_level_shifter with the C++ IC's declared storage.

The shared replica bias is an external port. Explicit process bindings select
LV/HV geometries; neither the generic beta nor extracted wiring is rescaled.
"""
from collections import Counter, defaultdict
import math
from pathlib import Path
import re

import klayout.db as db

from capacitor_layout import append_mim
from core_driver_process import BINDINGS, contributions
from mos_layout import append_mos
from reference_clock_row import create_row
from reference_clock_placement import placement_order


PORTS = ['VDD', 'VSS', 'VLOGIC', 'BIAS', 'A', 'Y', 'Y_N']
ROLES = dict(logic_input_n=1, logic_input_p=1, logic_pull_down=2,
             cascode=2, latch=2, gate_buffer_n=2, gate_buffer_p=2)


def circuit(source):
    text = Path(source).read_text()
    if 'module OneGTransmitTranslator(' + ', '.join(PORTS) + ');' not in text:
        raise ValueError('requires the standalone C++ transmitter translator')
    names = ['VSS', *[p for p in PORTS if p != 'VSS'],
             *sorted(set(re.findall(r'\bcpphdl_n\d+\b', text)))]
    mapping = {name: 'n' + str(index) for index, name in enumerate(names)}
    text = re.sub(r'\b(?:' + '|'.join(names) + r')\b', lambda m: mapping[m[0]], text)
    capacitor = re.compile(r'^\s*I\((n\d+), (n\d+)\) <\+ ([-+0-9.eE]+) \* '
        r'ddt\(V\((n\d+), (n\d+)\)\); // Bench initial voltage = 0.0 V\.$', re.M)
    capacitors = defaultdict(float)
    for match in capacitor.finditer(text):
        a, b, value, va, vb = match.groups()
        value = float(value)
        if (a, b) != (va, vb) or a == b or not math.isfinite(value) or value <= 0:
            raise ValueError('invalid translator storage branch')
        # Capacitor orientation does not change its two-terminal law.
        capacitors[tuple(sorted((a, b)))] += value
    bindings = tuple((*row[:-1], ROLES[row[0]]) for row in BINDINGS if row[0] in ROLES)
    devices, roles = contributions(capacitor.sub('', text), bindings)
    if roles != Counter(ROLES) or len(devices) != 12:
        raise ValueError('translator process device inventory changed')
    if len(capacitors) != 19:
        raise ValueError('translator storage inventory changed')
    rename = {mapping[p]: p for p in PORTS} | {'0': 'VSS'}

    def net(name):
        return rename.get(name, name.upper())

    for device in devices:
        for key in ('a', 'b', 'gate', 'body'):
            device[key] = net(device[key])
    return devices, [(net(a), net(b), value)
                     for (a, b), value in sorted(capacitors.items())]


def realization(source, storage='declared'):
    devices, capacitors = circuit(source)
    if storage == 'declared':
        return devices, capacitors, dict(storage=storage, physical_storage_branches=len(capacitors),
            scope='literal realization of all C++ storage, in addition to PSP intrinsic charge')
    if storage != 'psp-charge':
        raise ValueError('unreviewed translator storage realization')
    # This is a whole-IC process implementation, not removal of capacitors
    # from a literal netlist. PSP supplies the base class's transistor charge;
    # the derived class's deliberate output compensation remains physical.
    records = re.findall(r'^\s*// device (\d+), IC [^,]+, ([^\n]+):\d+\n\s*'
                         r'I\((\w+), (\w+)\) <\+ ([-+0-9.eE]+) \* '
                         r'ddt\(V\((\w+), (\w+)\)\); // Bench initial voltage = 0.0 V\.$',
                         Path(source).read_text(), re.M)
    compensation, modeled = [], []
    for identity, origin, a, b, value, va, vb in records:
        if (a, b) != (va, vb):
            raise ValueError('translator charge provenance has inconsistent terminals')
        filename = Path(origin).name
        if filename == 'clock_level_shifter.hpp':
            compensation.append((a, b, float(value)))
        elif filename == 'cascoded_level_shifter.hpp':
            modeled.append(int(identity))
        else:
            raise ValueError('translator storage has an unreviewed owner')
    if (len(modeled) != 22 or len(compensation) != 2 or
            {(a, b) for a, b, _ in compensation} != {('Y', 'VSS'), ('Y_N', 'VSS')} or
            any(not math.isclose(value, 200e-15, rel_tol=1e-12) for _, _, value in compensation)):
        raise ValueError('translator charge/compensation ownership changed')
    return devices, compensation, dict(storage=storage, physical_storage_branches=2,
        generic_charge_elements_replaced=modeled,
        scope='whole-IC PSP realization: intrinsic terminal charge plus two intentional 200 fF output capacitors; functional contracts require independent qualification')


def create_transmit_translator(pdk, source, storage='declared'):
    devices, capacitors, implementation = realization(source, storage)
    latch_nodes = sorted({d['a'] for d in devices if d['role'] == 'latch'})
    local_nodes = sorted({d['a'] for d in devices if d['role'] in
                          ('logic_input_n', 'logic_pull_down')})

    def order_parts(parts, dbu):
        # Regenerative nodes pay the highest interconnect-charge cost. The
        # external replica can drive a longer bias bus without spreading the
        # latch devices apart merely to shorten that shared connection.
        weights = {net: 3 for net in local_nodes}
        weights.update({net: 8 for net in latch_nodes})
        weights.update(BIAS=.05, A=2, Y=2, Y_N=2)
        return placement_order(parts, dbu, power_nets=('VDD', 'VSS', 'VLOGIC'),
                               net_weights=weights)

    def parts(layout):
        result = []
        for index, device in enumerate(devices):
            model = device['model']
            width, length = device['width_um'], device['length_um']
            cell, info = append_mos(layout, pdk, 'TranslatorMOS' + str(index),
                width, length, math.ceil(width / 5),
                'p' if model.endswith('pmos') else 'n',
                'hv' if '_hv_' in model else 'lv')
            # Cascode sources carry signal current too. Preserve their private
            # nets while giving every source stripe the same parallel access.
            reinforce_source_contacts(layout, cell, info)
            reinforce_gate_contacts(layout, cell, info)
            if device['a'] in ('Y', 'Y_N'):
                reinforce_output_drain_contacts(layout, cell, info)
            result.append((cell, info, dict(D=device['a'], G=device['gate'],
                S=device['b'], B=device['body']), 'mos'))
        for index, (a, b, value) in enumerate(capacitors):
            cell, info = append_mim(layout, pdk, 'TranslatorStorage' + str(index), value)
            result.append((cell, info, dict(PLUS=a, MINUS=b), 'capacitor'))
        return result

    layout, top, info = create_row('TransmitTranslator', PORTS, parts, order_parts=order_parts,
        priority_nets=(*latch_nodes, *local_nodes, 'A', 'Y', 'Y_N', 'BIAS'),
        via_columns=1, signal_width_um=.24)
    widen_bias_bus(layout, top, info)
    add_supply_returns(layout, top, info)
    strap_supply_rails(layout, top, info)
    add_output_routes(layout, top, info)
    reinforce_internal_gate_vias(layout, top, info)
    if storage == 'psp-charge':
        # Literal storage adds terminals on this net and retains row routing.
        inverter_output = next(d['a'] for d in devices if d['role'] == 'logic_input_n')
        route_local_inverter_output(layout, top, info, inverter_output)
        pull_downs = sorted(d['a'] for d in devices if d['role'] == 'logic_pull_down')
        route_local_pull_downs(layout, top, info, pull_downs)
    info['process_realization'] = implementation
    info['intended_device_inventory'] = dict(MOS=len(devices), storage_branches=len(capacitors))
    info.update(width_um=top.bbox().width() * layout.dbu,
                height_um=top.bbox().height() * layout.dbu,
                bounding_area_um2=top.bbox().area() * layout.dbu ** 2)
    return layout, top, info


def reinforce_source_contacts(layout, cell, info):
    """Reach source diffusion along each stripe with real via arrays."""
    instances = list(cell.each_inst())
    if len(instances) != 1:
        raise ValueError('translator MOS PCell hierarchy changed')
    device = instances[0].cell
    width = info['finger_width_um']
    stripes = {s.bbox().to_s(): s.bbox()
               for s in device.each_shape(layout.layer(8, 0))
               if s.is_box() and s.bbox().height() == round(width / layout.dbu)}
    stripes = sorted(stripes.values(), key=lambda box: box.left)
    if len(stripes) != info['fingers'] + 1 or width != 5:
        raise ValueError('unreviewed translator source stripe geometry')
    ys = [.16 + .42 * index for index in range(10)]
    source_y = info['terminals_um']['S'][1]
    if source_y != .45:
        raise ValueError('translator source access height changed')

    def box(layer, x0, y0, x1, y1):
        cell.shapes(layout.layer(layer, 0)).insert(db.Box(*(round(v / layout.dbu)
            for v in (x0, y0, x1, y1))))

    for stripe in stripes[::2]:
        x = stripe.center().x * layout.dbu
        # Move the original cut as part of the array: retaining it between
        # the first two new cuts would violate Via1 spacing. The bottom cut
        # reduces the narrow Metal1 path from the diffusion contact row.
        original = db.Point(round(x / layout.dbu), round(source_y / layout.dbu))
        cuts = [shape for shape in cell.each_shape(layout.layer(19, 0))
                if shape.bbox().center() == original]
        if len(cuts) != 1:
            raise ValueError('ambiguous translator source via')
        cuts[0].delete()
        box(8, x - .16, ys[0] - .16, x + .16, ys[-1] + .16)
        box(10, x - .2, ys[0] - .2, x + .2, ys[-1] + .2)
        for y in ys:
            box(19, x - .095, y - .095, x + .095, y + .095)
        # Carry each stripe's current into the Metal3 collector through a
        # distributed Via2 array, avoiding a single cut after the Via1 array.
        upper_ys = [source_y + .42 * index for index in range(9)]
        box(30, x - .2, upper_ys[0] - .2, x + .2, upper_ys[-1] + .2)
        for y in upper_ys:
            box(29, x - .095, y - .095, x + .095, y + .095)
    source_xs = [stripe.center().x * layout.dbu for stripe in stripes[::2]]
    box(30, min(source_xs) - .2, source_y - .62, max(source_xs) + .2, source_y + .62)
    info['supply_source_extent_um'] = [min(source_xs), max(source_xs)]
    info['supply_source_vias_per_stripe'] = len(ys)
    info['supply_source_via2_per_stripe'] = len(upper_ys)



def reinforce_output_drain_contacts(layout, cell, info):
    """Spread output current over diffusion contacts before the Metal3 escape."""
    instances = list(cell.each_inst())
    if len(instances) != 1 or info['finger_width_um'] != 5 or '_hv_' not in info['model']:
        raise ValueError('unreviewed output drain geometry')
    device = instances[0].cell
    stripes = {shape.bbox().to_s(): shape.bbox()
               for shape in device.each_shape(layout.layer(8, 0))
               if shape.is_box() and shape.bbox().height() == round(5 / layout.dbu)}
    stripes = sorted(stripes.values(), key=lambda b: b.left)
    if len(stripes) != info['fingers'] + 1:
        raise ValueError('output diffusion stripe inventory changed')
    x0, y = info['terminals_um']['D']
    if y != 4.55:
        raise ValueError('output drain escape height changed')

    def box(layer, x0, y0, x1, y1):
        cell.shapes(layout.layer(layer, 0)).insert(db.Box(*(round(v / layout.dbu)
            for v in (x0, y0, x1, y1))))

    ys = [y - .42 * index for index in range(9)]
    for stripe in stripes[1::2]:
        x = stripe.center().x * layout.dbu
        original = db.Point(round(x / layout.dbu), round(y / layout.dbu))
        cuts = [shape for shape in cell.each_shape(layout.layer(19, 0))
                if shape.bbox().center() == original]
        if len(cuts) != 1:
            raise ValueError('ambiguous output diffusion via')
        cuts[0].delete()
        box(8, x - .16, min(ys) - .16, x + .16, max(ys) + .16)
        box(10, x - .2, min(ys) - .2, x + .2, max(ys) + .2)
        for cy in ys:
            box(19, x - .095, cy - .095, x + .095, cy + .095)
    # Parallel the existing escape without adding a long signal collector.
    box(30, x0 - .2, y - 1.04, x0 + .2, y + .2)
    for delta in (0, -.42, -.84):
        box(29, x0 - .095, y + delta - .095, x0 + .095, y + delta + .095)
    info['output_drain_via1_per_stripe'] = 9
    info['output_drain_escape_via2_cuts'] = 3


def reinforce_gate_contacts(layout, cell, info):
    """Parallel the gate's Via2 access without extending its horizontal bus."""
    instances = list(cell.each_inst())
    if len(instances) != 1 or info['finger_width_um'] != 5:
        raise ValueError('unreviewed translator gate geometry')
    device = instances[0].cell
    gates = {shape.bbox().to_s(): shape.bbox()
             for shape in device.each_shape(layout.layer(5, 0)) if shape.is_box()}
    if len(gates) != info['fingers']:
        raise ValueError('translator poly-gate inventory changed')
    xs = [gate.center().x * layout.dbu for gate in gates.values()]
    y = info['terminals_um']['G'][1]
    if y != -.6 or min(xs) != info['terminals_um']['G'][0]:
        raise ValueError('translator gate access location changed')

    def box(layer, x0, y0, x1, y1):
        cell.shapes(layout.layer(layer, 0)).insert(db.Box(*(round(v / layout.dbu)
            for v in (x0, y0, x1, y1))))

    x = min(xs)
    for layer in (10, 30):
        box(layer, x - .2, y - 1.04, x + .2, y + .2)
    for delta in (0, -.42, -.84):
        box(29, x - .095, y + delta - .095, x + .095, y + delta + .095)
    info['gate_via2_rows'] = 3


def reinforce_internal_gate_vias(layout, top, info):
    """Parallel the existing gate-column cuts while retaining short signal buses."""
    columns = []

    def box(layer, x0, y0, x1, y1):
        top.shapes(layout.layer(layer, 0)).insert(db.Box(*(round(v / layout.dbu)
            for v in (x0, y0, x1, y1))))

    for part in info['components']:
        if part['kind'] != 'mos' or part['connections']['G'] in PORTS:
            continue
        net = part['connections']['G']
        original = next(e for e in info['escapes']
                        if e['component'] == part['name'] and e['terminal'] == 'G')
        old_x = round(original['location_um'][0] / layout.dbu)
        cuts = [shape for shape in top.each_shape(layout.layer(66, 0))
                if shape.bbox().center().x == old_x and shape.bbox().center().y < 0]
        if len(cuts) != 1:
            raise ValueError('ambiguous internal gate bus access')
        bus_y = cuts[0].bbox().center().y * layout.dbu
        cuts[0].delete()
        x, y = original['location_um']
        box(30, x - .2, y - 1.04, x + .2, y + .2)
        box(50, x - .2, bus_y - .41, x + .2, y + .2)
        box(67, x - .2, bus_y - .41, x + .2, bus_y + .41)
        for delta in (-.42, -.84):
            box(49, x - .095, y + delta - .095, x + .095, y + delta + .095)
        for delta in (-.21, .21):
            box(66, x - .095, bus_y + delta - .095, x + .095, bus_y + delta + .095)
        columns.append(dict(component=part['name'], net=net, location_um=[x, y],
                            bus_y_um=bus_y, via3_cuts=3, via4_cuts=2))
    if len(columns) != 7:
        raise ValueError('internal gate route inventory changed')
    info['reinforced_internal_gate_columns'] = columns



def route_local_inverter_output(layout, top, info, net):
    """Connect the local inverter and pull-down gate above their device row."""
    points = [p for p in info['escapes'] if p['net'] == net]
    if len(points) != 3 or sorted(p['terminal'] for p in points) != ['D', 'D', 'G']:
        raise ValueError('local inverter output boundary changed')
    bus_heights = []
    for point in points:
        x = round(point['location_um'][0] / layout.dbu)
        cuts = [shape for shape in top.each_shape(layout.layer(66, 0))
                if shape.bbox().center().x == x and shape.bbox().center().y < 0]
        if not cuts:
            raise ValueError('missing original inverter-output bus cut')
        bus_heights.append(sum(shape.bbox().center().y for shape in cuts) / len(cuts))
        for shape in cuts:
            shape.delete()
        # Remove the old vertical route and its bottom landing, retaining
        # the Metal3-to-Metal4 contact at the real device escape.
        for shape in list(top.each_shape(layout.layer(50, 0))):
            box = shape.bbox()
            if box.center().x == x and box.bottom < 0 and box.width() <= round(.4 / layout.dbu):
                shape.delete()
    if max(bus_heights) != min(bus_heights):
        raise ValueError('inverter-output escapes do not share one bus')
    old_y = round(bus_heights[0])
    buses = [shape for shape in top.each_shape(layout.layer(67, 0))
             if shape.bbox().center().y == old_y]
    if len(buses) < 4:
        raise ValueError('incomplete inverter-output bus geometry')
    for shape in buses:
        shape.delete()
    route_y = 14
    xs = [p['location_um'][0] for p in points]
    for point in points:
        x, y = point['location_um']
        bottom = y - (1.04 if point['terminal'] == 'G' else .2)
        if y + .5 >= route_y:
            raise ValueError('local inverter output route crosses the device row')
        top.shapes(layout.layer(50, 0)).insert(db.DBox(x - .2, bottom, x + .2, route_y + .12))
    top.shapes(layout.layer(50, 0)).insert(db.DBox(min(xs) - .2, route_y - .12,
                                                max(xs) + .2, route_y + .12))
    info['local_inverter_output_route'] = dict(net=net, layer=50, y_um=route_y,
        connected_escapes=points, removed_bottom_bus_y_um=old_y * layout.dbu)



def route_local_pull_downs(layout, top, info, nets):
    """Keep each pull-down drain beside its cascode source on an upper bus."""
    routes = []
    if len(nets) != 2 or len(set(nets)) != 2:
        raise ValueError('pull-down net inventory changed')
    for net in nets:
        points = [p for p in info['escapes'] if p['net'] == net]
        if len(points) != 2 or sorted(p['terminal'] for p in points) != ['D', 'S']:
            raise ValueError('local pull-down boundary changed')
        heights = []
        for point in points:
            x = round(point['location_um'][0] / layout.dbu)
            cuts = [shape for shape in top.each_shape(layout.layer(66, 0))
                    if shape.bbox().center().x == x and shape.bbox().center().y < 0]
            if len(cuts) != 1:
                raise ValueError('ambiguous original pull-down via')
            heights.append(cuts[0].bbox().center().y)
            cuts[0].delete()
            for shape in list(top.each_shape(layout.layer(50, 0))):
                box = shape.bbox()
                if box.center().x == x and box.bottom < 0 and box.width() <= round(.4 / layout.dbu):
                    shape.delete()
        if heights[0] != heights[1]:
            raise ValueError('pull-down terminals have different buses')
        buses = [shape for shape in top.each_shape(layout.layer(67, 0))
                 if shape.bbox().center().y == heights[0]]
        if len(buses) != 3:
            raise ValueError('incomplete original pull-down bus')
        for shape in buses:
            shape.delete()
        route_y = 13
        xs = [p['location_um'][0] for p in points]
        for point in points:
            x, y = point['location_um']
            if y + .5 >= route_y:
                raise ValueError('pull-down route has no upper clearance')
            top.shapes(layout.layer(50, 0)).insert(db.DBox(x - .2, y - .2, x + .2, route_y + .2))
            top.shapes(layout.layer(67, 0)).insert(db.DBox(x - .2, route_y - .2, x + .2, route_y + .2))
            top.shapes(layout.layer(66, 0)).insert(db.DBox(x - .095, route_y - .095,
                                                        x + .095, route_y + .095))
        top.shapes(layout.layer(67, 0)).insert(db.DBox(min(xs) - .2, route_y - .12,
                                                    max(xs) + .2, route_y + .12))
        routes.append(dict(net=net, layer=67, y_um=route_y, connected_escapes=points,
                           removed_bottom_bus_y_um=heights[0] * layout.dbu))
    info['local_pull_down_routes'] = routes


def add_supply_returns(layout, top, info):
    """Connect supply-fed sources directly above each transistor bank.

    Generic gutter routing leaves long narrow return columns. The transistor
    cells occupy only Metal1/2, so a separate Metal4 column can land on the
    actual source strap without crossing another device's signal column.
    """
    supplies = {'VDD', 'VSS', 'VLOGIC'}
    columns = []

    def box(layer, x0, y0, x1, y1):
        top.shapes(layout.layer(layer, 0)).insert(db.Box(*(round(v / layout.dbu)
            for v in (x0, y0, x1, y1))))

    for part in info['components']:
        if part['kind'] != 'mos':
            continue
        terminal = 'S' if part['connections']['S'] in supplies else 'B'
        net = part['connections'][terminal]
        if net not in supplies:
            raise ValueError('translator supply return has no power connection')
        px, py = part['geometry']['terminals_um'][terminal]
        dx, dy = part['origin_um']
        x, y = px + dx, py + dy
        bus_y = info['terminals'][net]['y_um']
        if terminal == 'S':
            if part['connections']['B'] != net:
                raise ValueError('source/guard bridge would join distinct circuit nets')
            # Connect independent MOS ports in the parent circuit. Keeping
            # this outside the four-terminal child preserves its LVS contract.
            bx, by = part['geometry']['terminals_um']['B']
            bx, by = bx + dx, by + dy
            box(10, min(bx, x) - .2, by - .2, max(bx, x) + .2, by + .2)
            # Feed the middle of the source collector. The original left
            # escape remains connected, but no longer carries the far fingers'
            # entire return current through a narrow horizontal strap.
            extent = part['geometry']['supply_source_extent_um']
            x = dx + sum(extent) / 2
        # Three rows of eight cuts distribute current through both ends of
        # the broad supply column. Keep Metal2 access inside the guarded cell.
        box(30, x - 1.67, y - .62, x + 1.67, y + .62)
        left = x - 1.67
        if terminal == 'B':
            box(29, x - .095, y - .095, x + .095, y + .095)
            escape = next(p for p in info['escapes'] if p['component'] == part['name']
                          and p['terminal'] == 'B')
            left = min(left, escape['location_um'][0] - .2)
            # The widened return joins the original ground column. Its old
            # single bus cut would sit too close to the new eight-cut array.
            old = db.Point(round(escape['location_um'][0] / layout.dbu), round(bus_y / layout.dbu))
            cuts = [shape for shape in top.each_shape(layout.layer(66, 0))
                    if shape.bbox().center() == old]
            if len(cuts) != 1:
                raise ValueError('ambiguous redundant guard return via')
            cuts[0].delete()
        box(50, left, bus_y - .62, x + 1.67, y + .62)
        box(67, x - 1.67, bus_y - .62, x + 1.67, bus_y + .62)
        for index in range(8):
            delta = (index - 3.5) * .42
            for layer, cy in ((49, y), (66, bus_y)):
                for row in (-.42, 0, .42):
                    box(layer, x + delta - .095, cy + row - .095, x + delta + .095, cy + row + .095)
        # Extend the real supply bus if a source lies beyond its gutter taps.
        existing = [p['location_um'][0] for p in info['escapes'] if p['net'] == net]
        half = 2.5 if net in ('VDD', 'VSS') else .12
        box(67, min(*existing, x) - .2, bus_y - half,
            max(*existing, x) + .2, bus_y + half)
        columns.append(dict(component=part['name'], terminal=terminal, net=net, landing_um=[x, y],
                            bus_y_um=bus_y, width_um=round(x + 1.67 - left, 12),
                            parallel_cuts=24, source_guard_bridge=terminal == 'S'))
    if len(columns) != 12:
        raise ValueError('translator supply-source inventory changed')
    info['direct_supply_columns'] = columns


def strap_supply_rails(layout, top, info):
    """Carry bank return current on TopMetal1 above the existing supply buses."""
    rails = {}
    for net in ('VDD', 'VSS'):
        port = info['terminals'][net]
        y = port['y_um']
        xs = sorted({port['x_um'], *(column['landing_um'][0]
            for column in info['direct_supply_columns'] if column['net'] == net)})
        top.shapes(layout.layer(126, 0)).insert(db.DBox(min(xs) - 1.9, y - 1.9, max(xs) + 1.9, y + 1.9))
        for x in xs:
            top.shapes(layout.layer(67, 0)).insert(db.DBox(x - 1.89, y - 1.89, x + 1.89, y + 1.89))
            for i in range(4):
                for j in range(4):
                    cx, cy = x + (i - 1.5) * .84, y + (j - 1.5) * .84
                    top.shapes(layout.layer(125, 0)).insert(db.DBox(cx - .21, cy - .21, cx + .21, cy + .21))
        rails[net] = dict(width_um=3.8, y_um=y, taps_x_um=xs, cuts_per_tap=16)
    info['top_metal_supply_rails'] = rails


def add_output_routes(layout, top, info):
    """Give the output buffers separate wide columns in their right gutters."""
    columns = []

    def box(layer, x0, y0, x1, y1):
        top.shapes(layout.layer(layer, 0)).insert(db.Box(*(round(v / layout.dbu)
            for v in (x0, y0, x1, y1))))

    for part in info['components']:
        if part['kind'] != 'mos' or part['connections']['D'] not in ('Y', 'Y_N'):
            continue
        net = part['connections']['D']
        dx, dy = part['origin_um']
        px, py = part['geometry']['terminals_um']['D']
        x, y = px + dx, py + dy
        escape = dx + layout.cell(part['name']).bbox().right * layout.dbu + 1
        bus_y = info['terminals'][net]['y_um']
        box(30, x - .2, y - .2, escape + .83, y + .2)
        box(30, escape - .83, y - .62, escape + .83, y + .62)
        box(50, escape - .83, bus_y - .62, escape + .83, y + .62)
        box(67, escape - .83, bus_y - .62, escape + .83, bus_y + .62)
        for delta in (-.63, -.21, .21, .63):
            for layer, cy in ((49, y), (66, bus_y)):
                for row in (-.42, 0, .42):
                    box(layer, escape + delta - .095, cy + row - .095,
                        escape + delta + .095, cy + row + .095)
        columns.append(dict(component=part['name'], net=net, x_um=escape,
                            width_um=1.66, parallel_cuts=12))
    if len(columns) != 4:
        raise ValueError('translator output-buffer inventory changed')
    rails = {}
    for net in ('Y', 'Y_N'):
        xs = sorted({info['terminals'][net]['x_um'],
                     *(p['location_um'][0] for p in info['escapes'] if p['net'] == net),
                     *(p['x_um'] for p in columns if p['net'] == net)})
        y = info['terminals'][net]['y_um']
        # Join the original output bus even when the optional upper rails
        # cannot fit around interleaved capacitor banks.
        box(67, min(xs) - .2, y - .12, max(xs) + .2, y + .12)
        rails[net] = dict(xs=xs, y_um=y, bounds=[min(xs) - 1.9, y - .82, max(xs) + 1.9, y + .82])
    a, b = (rails[net]['bounds'] for net in ('Y', 'Y_N'))
    separated = max(b[0] - a[2], a[0] - b[2], b[1] - a[3], a[1] - b[3]) >= 1.64
    # Literal-storage placement may interleave the output capacitor banks.
    # Preserve their legal original buses when separate upper rails do not fit.
    if separated:
        for rail in rails.values():
            box(126, *rail['bounds'])
            for x in rail['xs']:
                y = rail['y_um']
                box(67, x - 1.89, y - .42, x + 1.89, y + .42)
                for delta in (-1.26, -.42, .42, 1.26):
                    box(125, x + delta - .21, y - .21, x + delta + .21, y + .21)
    info['output_current_routes'] = dict(columns=columns, upper_rails=rails if separated else {})


def widen_bias_bus(layout, top, info):
    """Supply cascode gate charge through a separate 5 um bias bus."""
    port = info['terminals']['BIAS']
    old_y = round(port['y_um'] / layout.dbu)
    pins = [p['location_um'] for p in info['escapes'] if p['net'] == 'BIAS']
    if len(pins) < 2 or port['layer'] != 67:
        raise ValueError('missing translator bias escapes')
    removed = Counter()
    for layer, datatype in ((67, 0), (66, 0), (67, 2), (67, 25)):
        for shape in list(top.each_shape(layout.layer(layer, datatype))):
            selected = (shape.text.string == 'BIAS' if shape.is_text()
                        else shape.bbox().center().y == old_y)
            if selected:
                removed[layer, datatype] += 1
                shape.delete()
    if removed[67, 25] != 1 or removed[67, 2] != 1 or removed[66, 0] != len(pins):
        raise ValueError('ambiguous translator bias route')
    y = top.bbox().bottom * layout.dbu - 4

    def box(layer, values):
        top.shapes(layout.layer(layer, 0)).insert(db.Box(*(round(v / layout.dbu) for v in values)))

    xs = [p[0] for p in pins]
    box(67, (min(xs) - .2, y - 2.5, max(xs) + .2, y + 2.5))
    for x, endpoint in pins:
        box(50, (x - .2, y - .2, x + .2, endpoint + .2))
        box(66, (x - .095, y - .095, x + .095, y + .095))
    x = min(xs)
    top.shapes(layout.layer(67, 2)).insert(db.DBox(x - .16, y - .16, x + .16, y + .16))
    top.shapes(layout.layer(67, 25)).insert(db.DText('BIAS', db.DTrans(x, y)))
    info['terminals']['BIAS'] = dict(layer=67, x_um=x, y_um=y)
    info['bias_bus'] = dict(width_um=5, y_um=y, connected_columns=len(pins))
    info.update(width_um=top.bbox().width() * layout.dbu,
                height_um=top.bbox().height() * layout.dbu,
                bounding_area_um2=top.bbox().area() * layout.dbu ** 2)
