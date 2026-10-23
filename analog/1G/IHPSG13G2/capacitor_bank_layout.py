"""Parallel MIM storage with physical straps for both plates."""
import math

import klayout.db as db

from capacitor_layout import append_mim


def append_mim_bank(layout, pdk, name, capacitance_F):
    """Use equal legal PCells, keeping their actual quantized storage visible.

    Individual plates stay below 5 pF, with 5 um clearance between cell
    boundaries. The two continuous straps are on distinct conductor layers.
    A bank's schematic explicitly contains every capacitor; labels alone do
    not connect its plates.
    """
    if (not math.isfinite(capacitance_F) or not 1e-14 <= capacitance_F <= 160e-12
            or not math.isclose(layout.dbu, .001) or layout.cell(name) is not None):
        raise ValueError('unsupported MIM bank storage, database unit or cell name')
    count = max(1, math.ceil(capacitance_F / 5e-12 - 1e-12))
    unit, info = append_mim(layout, pdk, name + '_unit', capacitance_F / count)
    cell = layout.create_cell(name)
    step = math.ceil((unit.bbox().width() * layout.dbu + 5) * 100) / 100
    for index in range(count):
        cell.insert(db.CellInstArray(unit.cell_index(), db.Trans(round(index * step / layout.dbu), 0)))
    terminals = {}
    straps = []
    for net, plate in info['terminals'].items():
        # Both layers permit 2 um metal here. The strap reaches the actual
        # conductive plate centre in every instance, including the last one.
        x0, y = plate['x_um'], plate['y_um']
        x1 = x0 + (count - 1) * step
        strap = db.Box(*(round(v / layout.dbu) for v in (x0 - 1, y - 1, x1 + 1, y + 1)))
        layer = plate['layer']
        cell.shapes(layout.layer(layer, 0)).insert(strap)
        cell.shapes(layout.layer(layer, 2)).insert(strap)
        cell.shapes(layout.layer(layer, 25)).insert(db.Text(net, db.Trans(round(x0 / layout.dbu), round(y / layout.dbu))))
        terminals[net] = dict(layer=layer, datatype=0, x_um=x0, y_um=y)
        straps.append(dict(net=net, layer=layer, box_dbu=[strap.left, strap.bottom, strap.right, strap.top]))
    schematic = f'.subckt {name} PLUS MINUS\n'
    schematic += ''.join(f'Xcap{i} PLUS MINUS {unit.name}\n' for i in range(count))
    schematic += f'.ends {name}\n' + info['schematic']
    nominal = count * info['pcell_nominal_capacitance_F']
    return cell, dict(requested_capacitance_F=capacitance_F, pcell_nominal_capacitance_F=nominal,
        capacitors=count, unit=info, pitch_um=step, terminals=terminals, straps=straps,
        bounding_area_um2=cell.bbox().area() * layout.dbu ** 2, schematic=schematic,
        scope='parallel MIM geometry and intended two-terminal connectivity; requires DRC/LVS and extracted performance checks')
