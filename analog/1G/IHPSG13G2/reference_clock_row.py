"""Route the reference PLL with ordered device groups and short sensitive-node buses.

This implementation retains physical terminals, conductor layers and substrate
contacts. Independent LVS/DRC and electrical extraction are required per macro.
"""
from collections import defaultdict
import math
import re

import klayout.db as db
from processing_clock_gate_layout import placement_order


def create_row(name, ports, build_parts, order_parts=None, priority_nets=(),
               via_columns=1, signal_width_um=.24, substrate_net='PSUB'):
    if via_columns not in (1, 2, 4) or not .24 <= signal_width_um <= 5:
        raise ValueError('unreviewed analog row route dimensions')
    if substrate_net != 'PSUB' and substrate_net not in ports:
        raise ValueError('shared substrate must be internal PSUB or an external port')
    layout = db.Layout()
    layout.dbu = .001
    top = layout.create_cell(name)
    components, definitions, instances, pins = [], [], [], defaultdict(list)
    escapes = []
    cursor = 0.
    via_half_x = .2 + .21 * (via_columns - 1)
    escape_pitch = .8 if via_columns == 1 else 2 * via_half_x + .74
    gutter = 5 if via_columns == 1 else 4 * escape_pitch + 1

    def box(layer, x0, y0, x1, y1):
        top.shapes(layout.layer(layer, 0)).insert(db.Box(*(round(v / layout.dbu)
            for v in (x0, y0, x1, y1))))

    def via(lower, upper, cut, x, y):
        for layer in (lower, upper):
            box(layer, x - via_half_x, y - .2, x + via_half_x, y + .2)
        for index in range(via_columns):
            dx = (index - (via_columns - 1) / 2) * .42
            box(cut, x + dx - .095, y - .095, x + dx + .095, y + .095)

    def place(cell, info, connections, kind):
        nonlocal cursor
        bounds = cell.bbox()
        dx = cursor + gutter - bounds.left * layout.dbu
        dy = 4 - bounds.bottom * layout.dbu
        top.insert(db.CellInstArray(cell.cell_index(), db.Trans(round(dx / layout.dbu), round(dy / layout.dbu))))
        terminals = info.get('terminals', {})
        if kind == 'mos':
            terminals = {name: dict(layer=10, x_um=p[0], y_um=p[1])
                         for name, p in info['terminals_um'].items()}
        for index, (name, net) in enumerate(connections.items()):
            pin = terminals[name]
            x, y = pin['x_um'] + dx, pin['y_um'] + dy
            if kind == 'macro':
                # Reviewed native clock macros reserve distinct Metal4
                # columns at their Metal3 pins. Keep those local accesses;
                # sending every port through one device gutter would short
                # ports that legitimately share a horizontal track.
                if pin['layer'] != 30 or not info.get('metal4_columns_clear'):
                    raise ValueError('unreviewed native macro pin access')
                escape = x
                via(30, 50, 49, x, y)
            elif kind in ('mos', 'resistor', 'diode'):
                # Distinct horizontal Metal3 escapes reach dedicated Metal4
                # columns in this device's gutter. No route crosses a peer
                # device's escape columns on the same conductor layer.
                if kind == 'mos' and name == 'B':
                    # On a 1 um finger the body landing lies only 0.4 um from
                    # either diffusion landing in y. Move it along the outer
                    # guard before escaping, so the Metal3 straps cannot join.
                    above = dy + max(p[1] for p in info['terminals_um'].values()) + 1
                    box(10, x - .2, y - .2, x + .2, above + .2)
                    y = above
                elif kind == 'diode' and name == info['guard_terminal']:
                    above = dy + max(p['y_um'] for p in terminals.values()) + 1
                    box(10, x - .2, y - .2, x + .2, above + .2)
                    y = above
                elif kind == 'resistor' and info['model'] == 'rhigh' and name == 'MINUS':
                    # The meander endpoints share y. Move the far terminal
                    # upward before its Metal3 escape to avoid joining them.
                    box(10, x - .2, y - .2, x + .2, y + 1.2)
                    y += 1
                escape = cursor + 1 + index * escape_pitch
                via(10, 30, 29, x, y)
                box(30, min(escape, x) - .2, y - .2, max(escape, x) + .2, y + .2)
                via(30, 50, 49, escape, y)
            elif pin['layer'] == 67:
                # The external bottom plate of a series pair is the right
                # capacitor. Escaping left would short its internal junction.
                escape = dx + bounds.right * layout.dbu + 1.2
                box(67, x - .2, y - .2, escape + .2, y + .2)
                via(50, 67, 66, escape, y)
            elif pin['layer'] == 126:
                # TopMetal1 minimum width is 1.64 um. Drop down outside the
                # MIM plate so TopVia1 cannot short its two electrodes.
                escape = dx + bounds.right * layout.dbu + 3
                above = dy + bounds.top * layout.dbu + 3
                # Rise above both plates before crossing the second unit's
                # top electrode, which belongs to the series junction.
                box(126, x - 1, y - 1, x + 1, above + 1)
                box(126, x - 1, above - 1, escape + 1, above + 1)
                y = above
                box(125, escape - .21, y - .21, escape + .21, y + .21)
                box(67, escape - .4, y - .4, escape + .4, y + .4)
                via(50, 67, 66, escape, y)
            else:
                raise ValueError('unsupported physical terminal layer')
            pins[net].append((escape, y))
            escapes.append(dict(component=cell.name, terminal=name, net=net,
                                location_um=[escape, y], kind=kind))
        definition = info['schematic']
        extra_terminal = ''
        if kind == 'mos' and info['model'] in ('sg13_lv_nmos', 'sg13_hv_nmos'):
            # NMOS bodies share the physical p-substrate. Each guard contact
            # retains its own tap device, while its substrate side connects
            # to one internal chip net, not an isolated per-device body.
            definition = definition.replace(' D G S B\n', ' D G S B SUB\n')
            definition = re.sub(r'\bbody\b', 'SUB', definition)
            extra_terminal = ' ' + substrate_net
        if kind == 'resistor':
            definition = definition.replace(' PLUS MINUS SUB\n', ' PLUS MINUS SUB BULK\n')
            definition = re.sub(r'\bbody\b', 'BULK', definition)
            extra_terminal = ' ' + substrate_net
        if kind == 'diode' and info['model'] == 'dantenna':
            definition = definition.replace(' PLUS MINUS\n', ' PLUS MINUS BULK\n')
            definition = re.sub(r'\bbody\b', 'BULK', definition)
            extra_terminal = ' ' + substrate_net
        definitions.append(definition)
        instances.append('X' + str(len(instances)) + ' ' + ' '.join(connections.values()) + extra_terminal + ' ' + cell.name)
        components.append(dict(name=cell.name, kind=kind, geometry=info, connections=connections,
                               origin_um=[dx, dy]))
        cursor = math.ceil((dx + bounds.right * layout.dbu + 5) * 100) / 100

    parts = build_parts(layout)
    order, placement = (order_parts or placement_order)(parts, layout.dbu)
    if sorted(order) != list(range(len(parts))):
        raise ValueError('analog placement must contain each physical component exactly once')
    row_starts = set(placement.get('row_start_components', []))
    if row_starts and (not row_starts <= {cell.name for cell, _, _, _ in parts}
                       or placement.get('row_width_um', 0) <= 0):
        raise ValueError('invalid hierarchical analog row boundaries')
    for index in order:
        if parts[index][0].name in row_starts:
            width = placement['row_width_um']
            cursor = math.ceil(cursor / width) * width
        place(*parts[index])
    terminals = {}
    previous_bottom = -1.
    priority = {net: i for i, net in enumerate(priority_nets)}
    for net, points in sorted(pins.items(), key=lambda item: (priority.get(item[0], len(priority)), item[0])):
        # Broad supply rails limit actual IR drop. Signal buses use legal
        # narrow Metal5 to reduce charge and gate-oxide antenna exposure.
        half = 2.5 if net in ('VDD', 'VSS') else signal_width_um / 2
        y = previous_bottom - .76 - half
        previous_bottom = y - half
        xs = [point[0] for point in points]
        box(67, min(xs) - .2, y - half, max(xs) + .2, y + half)
        for x, endpoint in points:
            box(50, x - via_half_x, y - .2, x + via_half_x, endpoint + .2)
            via(50, 67, 66, x, y)
        if net in ports:
            x = min(xs)
            top.shapes(layout.layer(67, 2)).insert(db.Box(*(round(v / layout.dbu)
                for v in (x - .16, y - .16, x + .16, y + .16))))
            top.shapes(layout.layer(67, 25)).insert(db.Text(net, db.Trans(round(x / layout.dbu), round(y / layout.dbu))))
            terminals[net] = dict(layer=67, x_um=x, y_um=y)
    if set(terminals) != set(ports):
        raise ValueError('unconnected analog macro port')
    schematic = '\n'.join(definitions) + '\n.SUBCKT ' + name + ' ' + ' '.join(ports) + '\n'
    schematic += '\n'.join(instances) + '\n.ENDS ' + name + '\n'
    return layout, top, dict(components=components, terminals=terminals, escapes=escapes,
        schematic=schematic, placement=placement, width_um=top.bbox().width() * layout.dbu,
        height_um=top.bbox().height() * layout.dbu, bounding_area_um2=top.bbox().area() * layout.dbu ** 2,
        via_columns=via_columns, signal_width_um=signal_width_um, substrate_net=substrate_net,
        scope='connected physical analog topology; requires DRC/LVS and extracted electrical qualification')
