"""Connected cascoded PAM5 current cell with upper-metal current paths."""
import math

import klayout.db as db

from mos_bank_layout import append_hv_nmos_bank


PORTS = ['P', 'N', 'GP', 'GN', 'BIAS', 'CASCODE', 'VSS']


def append_transmit_cell(layout, pdk, name, history=False):
    """Route one main/history cell from cascoded_emphasized_pam5_driver.

    Each row holds a guarded device bank and four isolated routing tracks.
    TopMetal1 carries horizontal current; TopMetal2 joins rows. Distributed
    TopVia2 cuts carry each branch. Gate and bulk access first leaves the guarded
    bank on Metal2, then rises outside its lower-metal geometry.
    """
    if not math.isclose(layout.dbu, .001) or layout.cell(name) is not None:
        raise ValueError('unsupported current-cell database unit or name')
    top = layout.create_cell(name)
    widths = (200, 500, 150) if history else (400, 1500, 450)
    devices = [('positive', widths[0], dict(D='P', G='GP', S='TAIL', B='VSS')),
               ('negative', widths[0], dict(D='N', G='GN', S='TAIL', B='VSS')),
               ('cascode', widths[1], dict(D='TAIL', G='CASCODE', S='LOWER', B='VSS')),
               ('sink', widths[2], dict(D='LOWER', G='BIAS', S='VSS', B='VSS'))]
    # Put the return and lower-current-node routes nearest the devices:
    # their series drop directly perturbs the current-setting Vgs/Vds.
    nets = ['VSS', 'LOWER', 'TAIL', 'P', 'N', 'GP', 'GN', 'BIAS', 'CASCODE']
    spine_x = {net: -40. - 20 * index for index, net in enumerate(nets)}
    current_nets = {'P', 'N', 'TAIL', 'LOWER', 'VSS'}
    joins = {net: [] for net in nets}
    current_access = {}
    components, row_y = [], 0.

    def box(layer, x0, y0, x1, y1):
        top.shapes(layout.layer(layer, 0)).insert(db.Box(*(round(v / layout.dbu)
            for v in (x0, y0, x1, y1))))

    def upper_join(x, y, current=False, rows=1):
        # Nominal TopVia2 resistance is 1.1 ohms per cut. The cell must
        # distribute its current at all three transitions, not only inside
        # the device. Extend local D/S pads outwards to admit two rows.
        count, half = (8, 8) if current else (2, 2)
        height = 1.05 + (rows - 1) * .98
        box(126, x - half, y - height, x + half, y + height)
        box(134, x - half, y - height, x + half, y + height)
        for column in range(count):
            dx = (column - (count - 1) / 2) * 1.96
            for row in range(rows):
                dy = (row - (rows - 1) / 2) * 1.96
                box(133, x + dx - .45, y + dy - .45, x + dx + .45, y + dy + .45)

    for index, (role, width, binding) in enumerate(devices):
        cell, geometry = append_hv_nmos_bank(layout, pdk, name + role, width, .45, width // 5)
        bounds = cell.bbox()
        dy = row_y - bounds.bottom * layout.dbu
        for layer in (10, 126):
            cell.shapes(layout.layer(layer, 2)).clear()
            cell.shapes(layout.layer(layer, 25)).clear()
        top.insert(db.CellInstArray(cell.cell_index(), db.Trans(0, round(dy / layout.dbu))))
        for track, terminal in enumerate(('G', 'B', 'D', 'S')):
            net = binding[terminal]
            point = geometry['terminals'][terminal]
            x, y = point['x_um'], point['y_um'] + dy
            if terminal in ('G', 'B'):
                access = -10. if terminal == 'G' else -20.
                box(10, access - .42, y - .2, x + .2, y + .2)
                for layer in (10, 30, 50):
                    box(layer, access - .42, y - .42, access + .42, y + .42)
                box(67, access - .74, y - .32, access + .74, y + .32)
                for cut in (29, 49, 66):
                    for dx in (-.21, .21):
                        box(cut, access + dx - .095, y - .095, access + dx + .095, y + .095)
                for dx in (-.42, .42):
                    box(125, access + dx - .21, y - .21, access + dx + .21, y + .21)
            else:
                access = x + (-12 if terminal == 'D' else 12)
                box(126, min(x, access) - 8, y - 1.05, max(x, access) + 8, y + 1.05)
            branch_current = terminal in ('D', 'S')
            branch_half = 8 if branch_current else 2
            net_half = 8 if net in current_nets else 2
            join_y = y + (.98 if terminal == 'D' else -.98) if branch_current else y
            upper_join(access, join_y, branch_current, 2 if branch_current else 1)
            channel_y = dy + bounds.top * layout.dbu + 12 + track * 24
            box(134, access - branch_half, y - 1, access + branch_half, channel_y + branch_half)
            box(126, spine_x[net] - net_half, channel_y - net_half, access + branch_half, channel_y + net_half)
            upper_join(access, channel_y, branch_current, 2 if branch_current else 1)
            upper_join(spine_x[net], channel_y, net in current_nets, 2 if net in current_nets else 1)
            joins[net].append(channel_y)
            # Supply/line pads feed the branch that carries the current.
            # The shared return spine still connects every guard, but should
            # not add a long series path ahead of the sink's source.
            if net in ('P', 'N') or (role == 'sink' and terminal == 'S'):
                current_access[net] = (access, channel_y)
        components.append(dict(role=role, geometry=geometry, origin_um=[0, dy], nets=binding))
        row_y = dy + bounds.top * layout.dbu + 104
    terminals, internal_anchors = {}, {}
    for net, points in joins.items():
        x = spine_x[net]
        low, high = min(points), max(points)
        if net in PORTS and net not in current_access:
            high = row_y + 6
        half = 8 if net in current_nets else 2
        box(134, x - half, low - 2, x + half, high + 2)
        if net in PORTS:
            x, high = current_access.get(net, (x, high))
            top.shapes(layout.layer(134, 2)).insert(db.Box(*(round(v / layout.dbu)
                for v in (x - .5, high - .5, x + .5, high + .5))))
            top.shapes(layout.layer(134, 25)).insert(db.Text(net,
                db.Trans(round(x / layout.dbu), round(high / layout.dbu))))
            terminals[net] = dict(layer=134, x_um=x, y_um=high)
        else:
            internal_anchors[net] = dict(layer=134, x_um=x, y_um=high)
    schematic = f'.subckt {name} ' + ' '.join(PORTS) + '\n'
    for index, component in enumerate(components):
        geometry, binding = component['geometry'], component['nets']
        schematic += (f'M{index} {binding["D"]} {binding["G"]} {binding["S"]} body '
                      f'sg13_hv_nmos W={geometry["total_width_um"]}u L=.45u\n')
        for bank, unit in enumerate(geometry['banks']):
            g = unit['geometry']
            schematic += (f'Rtap{index}_{bank} VSS body ptap1 A={g["tap_area_um2"]:.12g}p '
                          f'P={g["tap_perimeter_um"]:.12g}u\n')
    schematic += f'.ends {name}\n'
    return top, dict(history=history, devices=components, terminals=terminals,
        internal_anchors=internal_anchors, schematic=schematic,
        width_um=top.bbox().width() * layout.dbu, height_um=top.bbox().height() * layout.dbu,
        bounding_area_um2=top.bbox().area() * layout.dbu ** 2,
        scope='connected current-cell geometry; requires DRC/LVS, extraction, electrical and EM qualification')
