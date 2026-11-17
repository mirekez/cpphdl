"""Two legal IHP MIM PCells in parallel for a 8--16 pF SAR storage node."""
import math

import klayout.db as db

from capacitor_layout import append_mim


def append_mim_parallel(layout, pdk, name, capacitance_F):
    if (not math.isfinite(capacitance_F) or not 8e-12 < capacitance_F <= 16e-12
            or layout.cell(name) is not None):
        raise ValueError('parallel MIM requires finite 8--16 pF storage')
    unit, info = append_mim(layout, pdk, name + '_unit', capacitance_F / 2)
    cell = layout.create_cell(name)
    pitch = math.ceil((unit.bbox().width() * layout.dbu + 5) * 100) / 100
    cell.insert(db.CellInstArray(unit.cell_index(), db.Trans()))
    cell.insert(db.CellInstArray(unit.cell_index(), db.Trans(round(pitch / layout.dbu), 0)))
    terminals = {}
    for pin in ('PLUS', 'MINUS'):
        plate = info['terminals'][pin]
        x, y, layer = plate['x_um'], plate['y_um'], plate['layer']
        # Each strip joins only like plates. Metal5 and TopMetal1 cross neither
        # a dielectric nor a via here; the PCell retains its own plate contacts.
        # TopMetal1 needs at least 1.64 um width (TM1.a). A 2 um strap also
        # keeps the two legal PCell plates joined after full-deck checking.
        half_width = 1.0
        cell.shapes(layout.layer(layer, 0)).insert(db.Box(
            round((x-half_width)/layout.dbu), round((y-half_width)/layout.dbu),
            round((x+pitch+half_width)/layout.dbu), round((y+half_width)/layout.dbu)))
        cell.shapes(layout.layer(layer, 2)).insert(db.Box(
            round((x-half_width)/layout.dbu), round((y-half_width)/layout.dbu),
            round((x+half_width)/layout.dbu), round((y+half_width)/layout.dbu)))
        cell.shapes(layout.layer(layer, 25)).insert(db.Text(
            pin, db.Trans(round(x/layout.dbu), round(y/layout.dbu))))
        terminals[pin] = dict(layer=layer, datatype=0, x_um=x, y_um=y)
    schematic = (f'.subckt {name} PLUS MINUS\n'
                 f'Xleft PLUS MINUS {unit.name}\nXright PLUS MINUS {unit.name}\n'
                 f'.ends {name}\n' + info['schematic'])
    return cell, dict(requested_capacitance_F=capacitance_F,
                      pcell_nominal_capacitance_F=2*info['pcell_nominal_capacitance_F'],
                      capacitors=2, configuration='parallel', unit=info,
                      pitch_um=pitch, terminals=terminals,
                      bounding_area_um2=cell.bbox().area()*layout.dbu**2,
                      schematic=schematic,
                      scope='two process MIM PCells, parallel metal bridges and real plate contacts')
