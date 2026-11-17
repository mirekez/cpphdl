"""A physical 0.5 fF MIM built from six legal 3 fF IHP plates in series."""

import math

import klayout.db as db
from capacitor_layout import append_mim


def append_half_ff(layout, pdk, name, capacitance_F):
    if (not math.isclose(capacitance_F, .5e-15, rel_tol=1e-12, abs_tol=0)
            or not math.isclose(layout.dbu, .001) or layout.cell(name) is not None):
        raise ValueError("requires 0.5 fF, unused name and 1 nm database unit")
    count = 6
    unit, info = append_mim(layout, pdk, name + "_unit", count * capacitance_F)
    cell = layout.create_cell(name)
    pitch = math.ceil((unit.bbox().width() * layout.dbu + 5) * 100) / 100
    top, bottom = info["terminals"]["PLUS"], info["terminals"]["MINUS"]
    junctions = []

    def box(layer, x0, y0, x1, y1):
        cell.shapes(layout.layer(layer, 0)).insert(db.Box(*(
            round(v / layout.dbu) for v in (x0, y0, x1, y1))))

    for index in range(count):
        shift = pitch * index
        cell.insert(db.CellInstArray(unit.cell_index(),
                                     db.Trans(round(shift / layout.dbu), 0)))
        if index == count - 1:
            continue
        x = shift + unit.bbox().right * layout.dbu + 2.5
        by, ty = bottom["y_um"], top["y_um"]
        box(67, shift + bottom["x_um"] - .4, by - .4, x + .4, by + .4)
        box(67, x - .4, min(by, ty) - .4, x + .4, max(by, ty) + .4)
        box(126, x - 1, ty - 1, shift + pitch + top["x_um"] + 1, ty + 1)
        box(125, x - .21, ty - .21, x + .21, ty + .21)
        junctions.append([x, ty])
    terminals = {}
    for pin, plate, shift in (("PLUS", top, 0),
                              ("MINUS", bottom, (count - 1) * pitch)):
        x, y, layer = plate["x_um"] + shift, plate["y_um"], plate["layer"]
        cell.shapes(layout.layer(layer, 2)).insert(db.Box(*(
            round(v / layout.dbu) for v in (x - .16, y - .16, x + .16, y + .16))))
        cell.shapes(layout.layer(layer, 25)).insert(db.Text(
            pin, db.Trans(round(x / layout.dbu), round(y / layout.dbu))))
        terminals[pin] = dict(layer=layer, datatype=0, x_um=x, y_um=y)
    lines = [f".subckt {name} PLUS MINUS"]
    nodes = ["PLUS"] + [f"MID{i}" for i in range(count - 1)] + ["MINUS"]
    lines.extend(f"X{i} {nodes[i]} {nodes[i+1]} {unit.name}" for i in range(count))
    lines.append(f".ends {name}")
    return cell, dict(requested_capacitance_F=capacitance_F,
                      pcell_nominal_capacitance_F=info["pcell_nominal_capacitance_F"] / count,
                      capacitors=count, unit=info, terminals=terminals,
                      junction_vias_um=junctions,
                      schematic="\n".join(lines) + "\n" + info["schematic"],
                      bounding_area_um2=cell.bbox().area() * layout.dbu**2,
                      scope="six physical MIM units in series; five floating junctions require extraction")
