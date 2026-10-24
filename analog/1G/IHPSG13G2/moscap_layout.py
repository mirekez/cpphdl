"""Connect a bank of actual two-terminal MOSCAP PCells on Metal2/Metal3.

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

    for net, pin, routing in [('G', pins[1], 10), ('SUB', pins[0], 30)]:
        x, y = pin.center().x * layout.dbu, pin.center().y * layout.dbu
        # One 190 nm Via1 lands on the existing native Metal1 contact.
        box(unit, 8, (x - .2, y - .2, x + .2, y + .2))
        box(unit, 19, (x - .095, y - .095, x + .095, y + .095))
        box(unit, 10, (x - .2, y - .2, x + .2, y + .2))
        if routing == 30:
            box(unit, 29, (x - .095, y - .095, x + .095, y + .095))
            box(unit, 30, (x - .2, y - .2, x + .2, y + .2))
        terminals[net] = dict(layer=routing, x_um=x, y_um=y)

    cell = layout.create_cell(name)
    columns = math.ceil(math.sqrt(copies))
    rows = math.ceil(copies / columns)
    step = math.ceil((max(unit.bbox().width(), unit.bbox().height()) * layout.dbu + 5) * 100) / 100
    for i in range(copies):
        cell.insert(db.CellInstArray(unit.cell_index(), db.Trans(round((i % columns) * step / layout.dbu),
                                                                round((i // columns) * step / layout.dbu))))
    straps = []
    trunk = unit.bbox().left * layout.dbu - 3
    for net, terminal in terminals.items():
        x, y, layer = terminal['x_um'], terminal['y_um'], terminal['layer']
        boxes = [(trunk - 1, y - 1, trunk + 1, y + (rows - 1) * step + 1)]
        for row in range(rows):
            last = min(columns, copies - row * columns) - 1
            boxes.append((trunk - 1, y + row * step - 1, x + last * step + 1, y + row * step + 1))
        for values in boxes:
            shape = box(cell, layer, values)
            straps.append(dict(net=net, layer=layer, box_dbu=[shape.left, shape.bottom, shape.right, shape.top]))
        marker = db.Box(round((trunk - 1) / layout.dbu), round((y - 1) / layout.dbu),
                        round((trunk + 1) / layout.dbu), round((y + 1) / layout.dbu))
        cell.shapes(layout.layer(layer, 2)).insert(marker)
        cell.shapes(layout.layer(layer, 25)).insert(db.Text(net, db.Trans(round(trunk / layout.dbu), round(y / layout.dbu))))
        terminal.update(x_um=trunk)
    schematic = f'.subckt {name} G SUB\n'
    schematic += ''.join(f'Xcap{i} G SUB {unit.name}\n' for i in range(copies))
    schematic += f'.ends {name}\n.subckt {unit.name} G SUB\n'
    schematic += f'Cstorage G SUB sg13_moscap_n w={width_um:g}u l={length_um:g}u\n.ends {unit.name}\n'
    return cell, dict(model='sg13_moscap_n', copies=copies, width_um=width_um, length_um=length_um,
        gate_area_um2=copies * width_um * length_um, bounding_area_um2=cell.bbox().area() * layout.dbu ** 2,
        columns=columns, rows=rows, pitch_um=step, terminals=terminals, straps=straps, schematic=schematic,
        scope='connected substrate-ground MOSCAP bank; requires DRC/LVS and voltage-dependent process qualification')
