"""Small storage from two legal MIM plates with a routed series junction."""
import math

import klayout.db as db

from capacitor_layout import append_mim


def append_mim_series(layout, pdk, name, capacitance_F):
    if (not math.isfinite(capacitance_F) or not 2e-15 <= capacitance_F <= 4e-15
            or not math.isclose(layout.dbu, .001) or layout.cell(name) is not None):
        raise ValueError('unsupported series MIM storage, database unit or name')
    unit, info = append_mim(layout, pdk, name + '_unit', 2 * capacitance_F)
    cell = layout.create_cell(name)
    # Leave room for a real TopVia1 outside both dielectric/plate extents.
    pitch = math.ceil((unit.bbox().width() * layout.dbu + 5) * 100) / 100
    cell.insert(db.CellInstArray(unit.cell_index(), db.Trans()))
    cell.insert(db.CellInstArray(unit.cell_index(), db.Trans(round(pitch / layout.dbu), 0)))
    top, bottom = info['terminals']['PLUS'], info['terminals']['MINUS']
    x = unit.bbox().right * layout.dbu + 2.5
    by, ty = bottom['y_um'], top['y_um']

    def box(layer, x0, y0, x1, y1):
        cell.shapes(layout.layer(layer, 0)).insert(db.Box(*(round(v / layout.dbu)
            for v in (x0, y0, x1, y1))))

    # First bottom plate -> Metal5 -> TopVia1 -> second top plate.
    box(67, bottom['x_um'] - .4, by - .4, x + .4, by + .4)
    box(67, x - .4, min(by, ty) - .4, x + .4, max(by, ty) + .4)
    box(126, x - 1, ty - 1, pitch + top['x_um'] + 1, ty + 1)
    box(125, x - .21, ty - .21, x + .21, ty + .21)
    terminals = {}
    for net, plate, shift in (('PLUS', top, 0), ('MINUS', bottom, pitch)):
        px, py, layer = plate['x_um'] + shift, plate['y_um'], plate['layer']
        cell.shapes(layout.layer(layer, 2)).insert(db.Box(*(round(v / layout.dbu)
            for v in (px - .16, py - .16, px + .16, py + .16))))
        cell.shapes(layout.layer(layer, 25)).insert(db.Text(net, db.Trans(round(px / layout.dbu), round(py / layout.dbu))))
        terminals[net] = dict(layer=layer, datatype=0, x_um=px, y_um=py)
    schematic = (f'.subckt {name} PLUS MINUS\n'
        f'Xupper PLUS MID {unit.name}\nXlower MID MINUS {unit.name}\n.ends {name}\n' + info['schematic'])
    return cell, dict(requested_capacitance_F=capacitance_F,
        pcell_nominal_capacitance_F=info['pcell_nominal_capacitance_F'] / 2,
        capacitors=2, unit=info, pitch_um=pitch, terminals=terminals,
        junction_via_um=[x, ty], schematic=schematic,
        bounding_area_um2=cell.bbox().area() * layout.dbu ** 2,
        scope='series MIM geometry; nominal equivalent excludes junction parasitics, leakage and mismatch')
