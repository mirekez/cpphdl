"""Physical small storage and segmented fine-DAC coupling resistance."""
import math

import klayout.db as db

from capacitor_layout import append_mim
from mos_layout import device_library


def append_one_ff(layout, pdk, name, capacitance_F=1e-15):
    """Three legal 3 fF plates, with two explicit floating series junctions."""
    if (not math.isclose(capacitance_F, 1e-15, rel_tol=1e-12, abs_tol=0)
            or not math.isclose(layout.dbu, .001) or layout.cell(name) is not None):
        raise ValueError('requires one femtofarad, unused name and 1 nm database unit')
    unit, info = append_mim(layout, pdk, name + '_unit', 3 * capacitance_F)
    cell = layout.create_cell(name)
    pitch = math.ceil((unit.bbox().width() * layout.dbu + 5) * 100) / 100

    def box(layer, x0, y0, x1, y1):
        cell.shapes(layout.layer(layer, 0)).insert(db.Box(*(round(v / layout.dbu)
            for v in (x0, y0, x1, y1))))

    top, bottom = info['terminals']['PLUS'], info['terminals']['MINUS']
    junctions = []
    for index in range(3):
        shift = pitch * index
        cell.insert(db.CellInstArray(unit.cell_index(), db.Trans(round(shift / layout.dbu), 0)))
        if index == 2:
            continue
        x = shift + unit.bbox().right * layout.dbu + 2.5
        by, ty = bottom['y_um'], top['y_um']
        box(67, shift + bottom['x_um'] - .4, by - .4, x + .4, by + .4)
        box(67, x - .4, min(by, ty) - .4, x + .4, max(by, ty) + .4)
        box(126, x - 1, ty - 1, shift + pitch + top['x_um'] + 1, ty + 1)
        box(125, x - .21, ty - .21, x + .21, ty + .21)
        junctions.append([x, ty])
    terminals = {}
    for net, plate, shift in (('PLUS', top, 0), ('MINUS', bottom, 2 * pitch)):
        x, y, layer = plate['x_um'] + shift, plate['y_um'], plate['layer']
        cell.shapes(layout.layer(layer, 2)).insert(db.Box(*(round(v / layout.dbu)
            for v in (x - .16, y - .16, x + .16, y + .16))))
        cell.shapes(layout.layer(layer, 25)).insert(db.Text(net, db.Trans(round(x / layout.dbu), round(y / layout.dbu))))
        terminals[net] = dict(layer=layer, x_um=x, y_um=y)
    schematic = (f'.subckt {name} PLUS MINUS\nX0 PLUS MID0 {unit.name}\n'
        f'X1 MID0 MID1 {unit.name}\nX2 MID1 MINUS {unit.name}\n.ends {name}\n' + info['schematic'])
    return cell, dict(requested_capacitance_F=capacitance_F,
        pcell_nominal_capacitance_F=info['pcell_nominal_capacitance_F'] / 3,
        capacitors=3, unit=info, terminals=terminals, junction_vias_um=junctions,
        schematic=schematic, bounding_area_um2=cell.bbox().area() * layout.dbu ** 2,
        scope='nominal series storage; two floating junctions require parasitic and leakage qualification')


def append_rhigh_unit(layout, pdk, name, resistance_Ohm):
    """A straight high-ohmic poly unit with explicit guard contact."""
    if (not math.isfinite(resistance_Ohm) or not 100000 <= resistance_Ohm <= 200000
            or not math.isclose(layout.dbu, .001) or layout.cell(name) is not None):
        raise ValueError('unsupported high-resistance unit')
    library = device_library(pdk)
    from sg13g2_pycell_lib.ihp.utility_functions import CbResCalc, CbResCurrent
    width, bends, spacing = .5, 0, 0.
    # Minimum legal width reduces the fine-control branch's substrate charge.
    # Keep the intended length on the PCell's 10 nm parameter grid.
    length = math.ceil(CbResCalc('l', resistance_Ohm, 0, width * 1e-6, bends, spacing * 1e-6, 'rhigh') * 1e8 - 1e-9) / 100
    nominal = CbResCalc('R', 0, length * 1e-6, width * 1e-6, bends, spacing * 1e-6, 'rhigh')
    if not resistance_Ohm <= nominal <= 1.01 * resistance_Ohm:
        raise ValueError('fine resistance rounding exceeds one percent')
    declaration = library.layout().pcell_declaration('rhigh')
    if declaration is None:
        raise ValueError('missing rhigh PCell')
    # The GUI callback requires a positive candidate spacing even with zero
    # bends. Straight devices have no spacing parameter in their LVS geometry.
    parameters = dict(w=f'{width:g}u', l=f'{length:g}u', b=bends, ps='2u', Calculate='R')
    probe = db.Layout()
    probe.dbu = layout.dbu
    core = probe.cell(probe.add_pcell_variant(library, declaration.id(), parameters)).bbox()
    index = layout.add_pcell_variant(library, declaration.id(), dict(parameters,
        guardRingType='psub', guardRingDistance='2u'))
    device = layout.cell(index)
    cell = layout.create_cell(name)
    cell.insert(db.CellInstArray(index, db.Trans()))
    terminals = {}

    def contact(net, x, y):
        for layer, half in ((8, .16), (19, .095), (10, .2)):
            cell.shapes(layout.layer(layer, 0)).insert(db.Box(*(round(v / layout.dbu)
                for v in (x - half, y - half, x + half, y + half))))
        cell.shapes(layout.layer(10, 2)).insert(db.Box(*(round(v / layout.dbu)
            for v in (x - .2, y - .2, x + .2, y + .2))))
        cell.shapes(layout.layer(10, 25)).insert(db.Text(net, db.Trans(round(x / layout.dbu), round(y / layout.dbu))))
        terminals[net] = dict(layer=10, x_um=x, y_um=y)

    pins = sorted([shape.bbox() for shape in device.each_shape(layout.layer(8, 2))], key=lambda p: p.bottom)
    if len(pins) != 2 or pins[0].center().y == pins[1].center().y:
        raise ValueError('changed straight resistor endpoint geometry')
    for net, pin in zip(('PLUS', 'MINUS'), pins):
        contact(net, pin.center().x * layout.dbu, pin.center().y * layout.dbu)
    ring = min((s.bbox() for s in device.each_shape(layout.layer(8, 0))
                if s.is_box() and s.bbox().height() > s.bbox().width()), key=lambda p: p.left)
    contact('SUB', ring.center().x * layout.dbu, ring.center().y * layout.dbu)
    outer_width, outer_height = core.width() * layout.dbu + 4, core.height() * layout.dbu + 4
    tap_area = .6 * (outer_width + outer_height - .6)
    tap_perimeter = 4 * (outer_width + outer_height - .6)
    schematic = (f'.subckt {name} PLUS MINUS SUB\n'
        f'Rpoly PLUS MINUS body rhigh W={width:g}u L={length:g}u B={bends} PS={spacing:g}u\n'
        f'Rtap SUB body ptap1 A={tap_area:.12g}p P={tap_perimeter:.12g}u\n.ends {name}\n')
    return cell, dict(model='rhigh', requested_resistance_Ohm=resistance_Ohm,
        pcell_nominal_resistance_Ohm=nominal, width_um=width, length_um=length, bends=bends, spacing_um=spacing,
        pcell_body_current_limit=CbResCurrent(width * 1e-6, .001, 'rhighG2'),
        terminals=terminals, schematic=schematic, bounding_area_um2=cell.bbox().area() * layout.dbu ** 2,
        scope='straight high-ohmic resistor geometry; requires process PVT and extraction')


def append_fine_resistor(layout, pdk, name, resistance_Ohm=5090000):
    """Thirty-two straight units in series, alternately mirrored for short joins.

    This avoids the pinned PDK's inconsistent bent-resistor length extraction.
    No extracted dimensions are copied back into the independent schematic.
    Every unit retains its real guard contact and shares the same p-substrate.
    """
    if not math.isclose(resistance_Ohm, 5090000, rel_tol=1e-12, abs_tol=0):
        raise ValueError('requires the 5.09 Mohm fine-DAC coupling')
    unit, info = append_rhigh_unit(layout, pdk, name + '_unit', resistance_Ohm / 32)
    if layout.cell(name) is not None:
        raise ValueError('duplicate fine resistor cell name')
    cell = layout.create_cell(name)
    bounds = unit.bbox()
    pitch = math.ceil((bounds.width() * layout.dbu + 2) * 100) / 100
    connections = []

    def rectangle(layer, x0, y0, x1, y1):
        cell.shapes(layout.layer(layer, 0)).insert(db.Box(x0, y0, x1, y1))

    for index in range(32):
        transform = db.Trans(0, bool(index % 2), round(index * pitch / layout.dbu) - bounds.left,
                            bounds.top if index % 2 else -bounds.bottom)
        cell.insert(db.CellInstArray(unit.cell_index(), transform))
        pins = {name: transform * db.Point(round(p['x_um'] / layout.dbu), round(p['y_um'] / layout.dbu))
                for name, p in info['terminals'].items()}
        connections.append(pins)
        if index:
            left, right = connections[index - 1]['MINUS'], pins['PLUS']
            if left.y != right.y:
                # Contact centres are not perfectly symmetric in the PCell.
                # An actual orthogonal Metal2 bend preserves the series path.
                rectangle(10, left.x - 200, min(left.y, right.y) - 200, left.x + 200, max(left.y, right.y) + 200)
            rectangle(10, left.x - 200, right.y - 200, right.x + 200, right.y + 200)
        # All substrate contacts join on Metal3, away from the endpoint joins.
        sub = pins['SUB']
        mid = bounds.height() // 2
        rectangle(10, sub.x - 200, min(sub.y, mid) - 200, sub.x + 200, max(sub.y, mid) + 200)
        rectangle(29, sub.x - 95, mid - 95, sub.x + 95, mid + 95)
        rectangle(30, sub.x - 200, mid - 200, sub.x + 200, mid + 200)
        if index:
            rectangle(30, connections[index - 1]['SUB'].x - 200, mid - 200, sub.x + 200, mid + 200)
    terminals = {}
    for name_, point, layer in [('PLUS', connections[0]['PLUS'], 10),
            ('MINUS', connections[-1]['MINUS'], 10), ('SUB', connections[0]['SUB'], 10)]:
        cell.shapes(layout.layer(layer, 2)).insert(db.Box(point.x - 160, point.y - 160, point.x + 160, point.y + 160))
        cell.shapes(layout.layer(layer, 25)).insert(db.Text(name_, db.Trans(point)))
        terminals[name_] = dict(layer=layer, x_um=point.x * layout.dbu, y_um=point.y * layout.dbu)
    definition = info['schematic'].replace(' PLUS MINUS SUB\n', ' PLUS MINUS SUB BULK\n').replace('body', 'BULK')
    schematic = definition + f'\n.subckt {name} PLUS MINUS SUB\n'
    for index in range(32):
        a, b = ('PLUS' if index == 0 else f'N{index}'), ('MINUS' if index == 31 else f'N{index + 1}')
        schematic += f'X{index} {a} {b} SUB body {unit.name}\n'
    schematic += f'.ends {name}\n'
    return cell, dict(model='rhigh', requested_resistance_Ohm=resistance_Ohm,
        pcell_nominal_resistance_Ohm=32 * info['pcell_nominal_resistance_Ohm'],
        units=32, unit=info, terminals=terminals, schematic=schematic,
        bounding_area_um2=cell.bbox().area() * layout.dbu ** 2,
        scope='32 straight high-ohmic poly devices with actual metal series joins and common substrate; PVT/PEX required')
