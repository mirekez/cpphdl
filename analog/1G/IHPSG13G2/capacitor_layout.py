"""Size an actual two-terminal IHP MIM PCell from requested storage.

Metal5 is the bottom plate; TopMetal1 is the contacted top plate. These are
routing terminals, not ideal wires crossing through the capacitor dielectric.
"""
import math
import json
from pathlib import Path

import klayout.db as db

from mos_layout import device_library


def append_mim(layout, pdk, name, capacitance_F):
    if (not math.isfinite(capacitance_F) or not 0 < capacitance_F <= 8e-12
            or not math.isclose(layout.dbu, .001) or layout.cell(name) is not None):
        raise ValueError('unsupported MIM storage, database unit or cell name')
    library = device_library(pdk)
    from sg13g2_pycell_lib.ihp.utility_functions import CbCapCalc
    technology = json.loads((Path(pdk) / 'ihp-sg13g2/libs.tech/klayout/python/sg13g2_pycell_lib/sg13g2_tech.json').read_text())
    parameters = technology['techParams']
    minimum_um = float(parameters['cmim_minLW'].removesuffix('u'))
    # Use the pinned technology's area, perimeter and linewidth terms. Round
    # upwards to its geometry grid; never silently supply less nominal storage.
    side = math.ceil(CbCapCalc('lw', capacitance_F, 0, 0, 'cmim') * 1e8 - 1e-9) / 100
    if side < minimum_um:
        raise ValueError('requested MIM storage is below the minimum plate geometry')
    width, length = side, side
    nominal = CbCapCalc('C', 0, side * 1e-6, side * 1e-6, 'cmim')
    if nominal > capacitance_F * 1.01 and side - .01 >= minimum_um:
        # At a few femtofarads, growing both sides by one grid step can exceed
        # 1%. Try the adjacent rectangle before rejecting the request.
        width = round(side - .01, 2)
        nominal = CbCapCalc('C', 0, length * 1e-6, width * 1e-6, 'cmim')
    if nominal < capacitance_F * (1 - 1e-12) or nominal > capacitance_F * 1.01 or nominal > 8e-12 or side * side > 5625:
        raise ValueError('MIM sizing exceeds rounding tolerance, maximum storage or plate area')
    declaration = library.layout().pcell_declaration('cmim')
    if declaration is None:
        raise ValueError('pinned MIM PCell is unavailable')
    index = layout.add_pcell_variant(library, declaration.id(), dict(Calculate='C', w=f'{width:g}u', l=f'{length:g}u'))
    cell = layout.create_cell(name)
    cell.insert(db.CellInstArray(index, db.Trans()))
    # Pin geometry and electrical labels must reach their own conductive plate.
    # Keep the PCell's actual via array and plate enclosures unchanged.
    marker = db.Region(layout.cell(index).begin_shapes_rec(layout.layer(36, 0))).bbox()
    if marker.width() != round(width / layout.dbu) or marker.height() != round(length / layout.dbu):
        raise ValueError('MIM PCell changed the requested plate dimensions')
    plates = {}
    for net, layer in (('PLUS', 126), ('MINUS', 67)):
        conductor = db.Region(layout.cell(index).begin_shapes_rec(layout.layer(layer, 0)))
        pins = conductor
        if pins.is_empty():
            raise ValueError('MIM PCell has missing or unconnected plate terminals')
        cell.shapes(layout.layer(layer, 2)).insert(pins)
        point = pins.bbox().center()
        cell.shapes(layout.layer(layer, 25)).insert(db.Text(net, db.Trans(point)))
        plates[net] = dict(layer=layer, datatype=0, x_um=point.x * layout.dbu, y_um=point.y * layout.dbu)
    return cell, dict(requested_capacitance_F=capacitance_F, pcell_nominal_capacitance_F=nominal,
        width_um=width, length_um=length, plate_area_um2=width * length,
        bounding_area_um2=cell.bbox().area() * layout.dbu ** 2, terminals=plates,
        schematic=f'.subckt {name} PLUS MINUS\nCstorage PLUS MINUS cap_cmim w={width:g}u l={length:g}u\n.ends {name}\n',
        scope='PCell nominal storage and strict plate connectivity; no extracted RC, corner or mismatch proof')
