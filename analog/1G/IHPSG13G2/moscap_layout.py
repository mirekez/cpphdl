"""Connect MOSCAP PCells through distributed contacts and upper-metal straps.

The N device's SUB terminal must connect to the chip substrate ground. This
generator does not provide an isolated floating capacitor or a precision MIM
replacement. Its capacitance and leakage require the process-device tests.
"""
import math

import klayout.db as db

from mos_layout import device_library


def append_moscap_bank(layout, pdk, name, copies=40, width_um=30, length_um=30):
    if (type(copies) is not int or not 1 <= copies <= 64 or not math.isclose(layout.dbu, .001)
            or layout.cell(name) is not None
            or not all(math.isfinite(v) and 1 <= v <= 30 and abs(v * 100 - round(v * 100)) < 1e-8
                       for v in (width_um, length_um))):
        raise ValueError('unsupported MOSCAP bank geometry')
    library = device_library(pdk)
    declaration = library.layout().pcell_declaration('moscap_n')
    if declaration is None:
        raise ValueError('pinned MOSCAP PCell unavailable')
    index = layout.add_pcell_variant(library, declaration.id(), dict(w=f'{width_um:g}u', l=f'{length_um:g}u', m='1'))
    device = layout.cell(index)
    unit = layout.create_cell(name + '_unit')
    unit.insert(db.CellInstArray(index, db.Trans()))
    pins = sorted([shape.bbox() for shape in device.each_shape(layout.layer(8, 2))], key=lambda box: box.bottom)
    if len(pins) != 2:
        raise ValueError('MOSCAP two-terminal PCell interface changed')
    terminals = {}

    def box(cell, layer, values):
        shape = db.Box(*(round(v / layout.dbu) for v in values))
        cell.shapes(layout.layer(layer, 0)).insert(shape)
        return shape

    def positions(low, high, pitch):
        count = max(1, math.floor((high - low) / pitch + 1e-9) + 1)
        center = (low + high) / 2
        return [center + (i - (count - 1) / 2) * pitch for i in range(count)]

    # Feed both native gate contact bars across their full length. The former
    # single Via1 at the upper bar lost most of the useful 1 GHz storage.
    m1 = [s.bbox() for s in device.each_shape(layout.layer(8, 0)) if s.is_box()]
    upper_gate = pins[1]
    bottom = [b for b in m1 if b.left == upper_gate.left and b.right == upper_gate.right and b.top < 0]
    if len(bottom) != 1:
        raise ValueError('MOSCAP two-ended gate contact geometry changed')
    gate_ys = []
    for bar in (upper_gate, bottom[0]):
        left, right = bar.left * layout.dbu, bar.right * layout.dbu
        y = bar.center().y * layout.dbu
        gate_ys.append(y)
        xs = positions(left + .2, right - .2, .42)
        box(unit, 8, (xs[0] - .16, y - .16, xs[-1] + .16, y + .16))
        for metal in (10, 30, 50):
            box(unit, metal, (xs[0] - .2, y - .2, xs[-1] + .2, y + .2))
        for x in xs:
            for cut in (19, 29, 49, 66):
                box(unit, cut, (x - .095, y - .095, x + .095, y + .095))
        box(unit, 67, (xs[0] - .31, y - .31, xs[-1] + .31, y + .31))
        upper_xs = positions(left + .63, right - .63, .84)
        for x in upper_xs:
            box(unit, 125, (x - .21, y - .21, x + .21, y + .21))
        box(unit, 126, (upper_xs[0] - .63, y - .82, upper_xs[-1] + .63, y + .82))
    center_x = upper_gate.center().x * layout.dbu
    low, high = min(gate_ys), max(gate_ys)
    box(unit, 126, (center_x - 1.05, low - 1.05, center_x + 1.05, high + 1.05))
    box(unit, 134, (center_x - 1, low - 1, center_x + 1, high + 1))
    for y in positions(low, high, 1.96):
        box(unit, 133, (center_x - .45, y - .45, center_x + .45, y + .45))
    terminals['G'] = dict(layer=134, x_um=center_x, y_um=(low + high) / 2)

    # Return charge through both contacted N+ side bars. Lift outside the
    # gate's upper-metal bars, then join the returns below the device.
    ground_y = -7.
    outside = []
    for side in ('left', 'right'):
        candidates = [b for b in m1 if b.height() > b.width()
                      and (b.right < 0 if side == 'left' else b.left > round(length_um / layout.dbu))]
        if not candidates:
            raise ValueError('MOSCAP contacted return bars missing')
        bar = max(candidates, key=lambda b: b.width())
        x = bar.center().x * layout.dbu
        lift_x = -3.5 if side == 'left' else length_um + 3.5
        outside.append(lift_x)
        ys = positions(bar.bottom * layout.dbu + .2, bar.top * layout.dbu - .2, .42)
        for metal in (10, 30):
            box(unit, metal, (x - .2, ys[0] - .2, x + .2, ys[-1] + .2))
        box(unit, 50, (min(x, lift_x) - .2, ys[0] - .2, max(x, lift_x) + .2, ys[-1] + .2))
        for y in ys:
            for cut in (19, 29, 49):
                box(unit, cut, (x - .095, y - .095, x + .095, y + .095))
            box(unit, 66, (lift_x - .095, y - .095, lift_x + .095, y + .095))
        box(unit, 67, (lift_x - .31, ys[0] - .31, lift_x + .31, ys[-1] + .31))
        for y in positions(ys[0], ys[-1], .84):
            box(unit, 125, (lift_x - .21, y - .21, lift_x + .21, y + .21))
        box(unit, 126, (lift_x - 1.05, ground_y - 1.05, lift_x + 1.05, ys[-1] + 1.05))
    box(unit, 126, (outside[0] - 1.05, ground_y - 1.05, outside[1] + 1.05, ground_y + 1.05))
    terminals['SUB'] = dict(layer=126, x_um=center_x, y_um=ground_y)

    cell = layout.create_cell(name)
    columns = math.ceil(math.sqrt(copies))
    rows = math.ceil(copies / columns)
    step = math.ceil((max(unit.bbox().width(), unit.bbox().height()) * layout.dbu + 8) * 100) / 100
    for i in range(copies):
        cell.insert(db.CellInstArray(unit.cell_index(), db.Trans(round((i % columns) * step / layout.dbu),
                                                                round((i // columns) * step / layout.dbu))))
    straps = []
    # A gate mesh uses the otherwise empty TopMetal2. Substrate return rows
    # on TopMetal1 join a mesh in the clear gaps between unit cells. Center
    # feeds avoid charging all forty devices through one long edge trunk.
    left = unit.bbox().left * layout.dbu - 4
    mesh_xs = [left + i * step for i in range(columns + 1)]
    trunk = mesh_xs[columns // 2]
    for net, terminal in terminals.items():
        x, y, layer = terminal['x_um'], terminal['y_um'], terminal['layer']
        if net == 'G':
            # Open windows retain metal density and slit-rule headroom.
            boxes = [(mx - 6, y - 11, mx + 6, y + (rows - 1) * step + 11) for mx in mesh_xs]
            boxes += [(mesh_xs[0] - 6, y + row * step - 11,
                       mesh_xs[-1] + 6, y + row * step + 11) for row in range(rows)]
        else:
            # Join the adjacent same-net return collectors into each column.
            # This uses the clear inter-cell gap instead of leaving two narrow
            # collectors beside a third narrow mesh wire.
            half = (step - (outside[1] - outside[0])) / 2 + 1.05
            boxes = [(mx - half, y - 4, mx + half,
                      ys[-1] + 1.05 + (rows - 1) * step) for mx in mesh_xs]
            boxes += [(mesh_xs[0] - half, y + row * step - 4,
                       mesh_xs[-1] + half, y + row * step + 4) for row in range(rows)]
        for values in boxes:
            shape = box(cell, layer, values)
            straps.append(dict(net=net, layer=layer, box_dbu=[shape.left, shape.bottom, shape.right, shape.top]))
        # Put the external landing on a full row strap. A midpoint between
        # rows would funnel the complete bank current through a thin column.
        pin_y = y + ((rows - 1) // 2) * step
        marker = db.Box(round((trunk - .8) / layout.dbu), round((pin_y - .8) / layout.dbu),
                        round((trunk + .8) / layout.dbu), round((pin_y + .8) / layout.dbu))
        cell.shapes(layout.layer(layer, 2)).insert(marker)
        cell.shapes(layout.layer(layer, 25)).insert(db.Text(net, db.Trans(round(trunk / layout.dbu), round(pin_y / layout.dbu))))
        terminal.update(x_um=trunk, y_um=pin_y)
    schematic = f'.subckt {name} G SUB\n'
    schematic += ''.join(f'Xcap{i} G SUB {unit.name}\n' for i in range(copies))
    schematic += f'.ends {name}\n.subckt {unit.name} G SUB\n'
    schematic += f'Cstorage G SUB sg13_moscap_n w={width_um:g}u l={length_um:g}u\n.ends {unit.name}\n'
    return cell, dict(model='sg13_moscap_n', copies=copies, width_um=width_um, length_um=length_um,
        gate_area_um2=copies * width_um * length_um, bounding_area_um2=cell.bbox().area() * layout.dbu ** 2,
        columns=columns, rows=rows, pitch_um=step, terminals=terminals, straps=straps, schematic=schematic,
        scope='connected substrate-ground MOSCAP bank; requires DRC/LVS and voltage-dependent process qualification')
