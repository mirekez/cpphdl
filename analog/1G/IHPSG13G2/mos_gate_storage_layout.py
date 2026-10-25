"""Parallel HV MOS gate storage with D/S/body physically joined to ground."""
import math
import re

import klayout.db as db

from mos_layout import append_mos


def append_hv_storage_bank(layout, pdk, name, copies=220):
    if type(copies) is not int or not 1 <= copies <= 220 or layout.cell(name) is not None:
        raise ValueError('unsupported HV gate-storage bank count')
    device, info = append_mos(layout, pdk, name + '_mos', 100, .5, 10, 'n', 'hv')
    unit = layout.create_cell(name + '_unit')
    unit.insert(db.CellInstArray(device.cell_index(), db.Trans()))

    def rectangle(cell, layer, x0, y0, x1, y1):
        shape = db.Box(*(round(v / layout.dbu) for v in (x0, y0, x1, y1)))
        cell.shapes(layout.layer(layer, 0)).insert(shape)
        return shape

    # A separate Metal3 network shorts the three ground terminals. The gate
    # stays on Metal2, so their geometric crossings do not short the capacitor.
    ground_x, ground_y = info['terminals_um']['B']
    for net in ('D', 'S', 'B'):
        x, y = info['terminals_um'][net]
        rectangle(unit, 10, x - .2, y - .2, x + .2, y + .2)
        rectangle(unit, 29, x - .095, y - .095, x + .095, y + .095)
        rectangle(unit, 30, min(x, ground_x) - .2, y - .2, max(x, ground_x) + .2, y + .2)
        rectangle(unit, 30, ground_x - .2, min(y, ground_y) - .2,
                  ground_x + .2, max(y, ground_y) + .2)
    gate_x, old_gate_y = info['terminals_um']['G']
    gate_y = device.bbox().bottom * layout.dbu - 3
    rectangle(unit, 10, gate_x - .2, gate_y - .2, gate_x + .2, old_gate_y + .2)
    columns = math.ceil(math.sqrt(copies))
    rows = math.ceil(copies / columns)
    pitch = math.ceil((max(unit.bbox().width(), unit.bbox().height()) * layout.dbu + 5) * 100) / 100
    top = layout.create_cell(name)
    for i in range(copies):
        top.insert(db.CellInstArray(unit.cell_index(), db.Trans(round((i % columns) * pitch / layout.dbu),
                                                              round((i // columns) * pitch / layout.dbu))))
    trunk = unit.bbox().left * layout.dbu - 4
    terminals = {}
    for net, layer, x, y in [('G', 10, gate_x, gate_y), ('SUB', 30, ground_x, ground_y)]:
        rectangle(top, layer, trunk - 2.5, y - 2.5, trunk + 2.5, y + (rows - 1) * pitch + 2.5)
        for row in range(rows):
            last = min(columns, copies - row * columns) - 1
            rectangle(top, layer, trunk - 2.5, y + row * pitch - 2.5,
                      x + last * pitch + 2.5, y + row * pitch + 2.5)
        pin = db.Box(*(round(v / layout.dbu) for v in (trunk - .5, y - .5, trunk + .5, y + .5)))
        top.shapes(layout.layer(layer, 2)).insert(pin)
        top.shapes(layout.layer(layer, 25)).insert(db.Text(net, db.Trans(round(trunk / layout.dbu), round(y / layout.dbu))))
        terminals[net] = dict(layer=layer, x_um=trunk, y_um=y)
    schematic = f'.subckt {name} G SUB\n'
    # Every unit occupies the same physical substrate. Carry its bulk through
    # the hierarchy as a shared internal net, retaining every finite tap.
    # Independent hidden body nodes in each subcircuit would describe a
    # different circuit from the assembled bank.
    schematic += ''.join(f'Xcap{i} G SUB bulk {unit.name}\n' for i in range(copies))
    schematic += f'.ends {name}\n.subckt {unit.name} G SUB BULK\n'
    mos_schematic = info['schematic']
    declaration = f'.SUBCKT {device.name} D G S B\n'
    if mos_schematic.count(declaration) != 1:
        raise ValueError('routed MOS schematic interface changed')
    mos_schematic = mos_schematic.replace(declaration, f'.SUBCKT {device.name} D G S B BULK\n')
    mos_schematic = re.sub(r'\bbody\b', 'BULK', mos_schematic)
    schematic += f'Xstorage SUB G SUB SUB BULK {device.name}\n.ends {unit.name}\n' + mos_schematic
    return top, dict(model='sg13_hv_nmos', copies=copies, width_um=100, length_um=.5, fingers=10,
        gate_area_um2=50 * copies, bounding_area_um2=top.bbox().area() * layout.dbu ** 2,
        columns=columns, rows=rows, pitch_um=pitch, terminals=terminals, schematic=schematic,
        scope='actual connected HV MOS gate storage; requires unmodified PDK DRC/LVS and extracted RC/charge qualification')
