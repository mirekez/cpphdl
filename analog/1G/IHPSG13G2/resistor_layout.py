"""Contacted IHP poly resistance with an explicit substrate connection."""
import math

import klayout.db as db

from mos_layout import device_library


def append_rppd(layout, pdk, name, resistance_Ohm, width_um=2):
    """Size a straight rppd using the pinned PDK's contact/linewidth terms.

    Width is an explicit design choice: a low resistance carrying line current
    needs different geometry from a bias resistor. This helper preserves that
    choice and never treats its nominal resistance as a PVT guarantee.
    """
    if (not all(math.isfinite(v) for v in (resistance_Ohm, width_um))
            or resistance_Ohm <= 0 or not 2 <= width_um <= 100
            or abs(width_um * 100 - round(width_um * 100)) > 1e-8
            or not math.isclose(layout.dbu, .001) or layout.cell(name) is not None):
        raise ValueError('unsupported resistor, width, database unit or cell name')
    library = device_library(pdk)
    from sg13g2_pycell_lib.ihp.utility_functions import CbResCalc, CbResCurrent
    length_um = math.ceil(CbResCalc('l', resistance_Ohm, 0, width_um * 1e-6, 0, 0, 'rppd') * 1e8 - 1e-9) / 100
    # The pinned Tcl callbacks format lengths >= 100 um as millimetres with
    # three decimal places. Select an upward whole-um length before producing
    # that PCell; otherwise its geometry can differ from the requested value.
    if length_um >= 100:
        length_um = math.ceil(length_um - 1e-9)
    if not .5 <= length_um <= 1000:
        raise ValueError('resistor length requires a different width or segmented design')
    nominal = CbResCalc('R', 0, length_um * 1e-6, width_um * 1e-6, 0, 0, 'rppd')
    if not resistance_Ohm * (1 - 1e-12) <= nominal <= resistance_Ohm * 1.01:
        raise ValueError('resistor quantization exceeds one percent')
    declaration = library.layout().pcell_declaration('rppd')
    if declaration is None:
        raise ValueError('pinned rppd PCell is unavailable')
    parameters = dict(w=f'{width_um:g}u', l=f'{length_um:g}u', b=0, Calculate='R')
    probe = db.Layout()
    probe.dbu = layout.dbu
    core = probe.cell(probe.add_pcell_variant(library, declaration.id(), parameters)).bbox()
    index = layout.add_pcell_variant(library, declaration.id(), dict(parameters,
        guardRingType='psub', guardRingDistance='2u'))
    device = layout.cell(index)
    cell = layout.create_cell(name)
    cell.insert(db.CellInstArray(index, db.Trans()))
    terminals = {}

    def rectangle(layer, x, y, dx, dy):
        cell.shapes(layout.layer(layer, 0)).insert(db.Box(*(round(v / layout.dbu)
            for v in (x - dx, y - dy, x + dx, y + dy))))

    def contact(net, x, y):
        rectangle(8, x, y, .16, .16)
        rectangle(19, x, y, .095, .095)
        rectangle(10, x, y, .2, .2)
        pin = db.Box(*(round(v / layout.dbu) for v in (x - .2, y - .2, x + .2, y + .2)))
        cell.shapes(layout.layer(10, 2)).insert(pin)
        cell.shapes(layout.layer(10, 25)).insert(db.Text(net, db.Trans(round(x / layout.dbu), round(y / layout.dbu))))
        terminals[net] = dict(layer=10, datatype=0, x_um=x, y_um=y)

    pins = sorted([shape.bbox() for shape in device.each_shape(layout.layer(8, 2))], key=lambda box: box.bottom)
    if len(pins) != 2:
        raise ValueError('rppd PCell changed its two conductive end contacts')
    # Its straight body ends 0.28 um inside each contact centre. Check the
    # generated geometry too: stored PCell parameters precede Tcl coercion.
    if pins[1].center().y - pins[0].center().y != round((length_um + .56) / layout.dbu):
        raise ValueError('rppd generated length differs from its schematic dimensions')
    for net, pin in zip(('PLUS', 'MINUS'), pins):
        contact(net, pin.center().x * layout.dbu, pin.center().y * layout.dbu)
    ring = min((shape.bbox() for shape in device.each_shape(layout.layer(8, 0))
                if shape.is_box() and shape.bbox().height() > shape.bbox().width()), key=lambda box: box.left)
    contact('SUB', ring.center().x * layout.dbu, ring.center().y * layout.dbu)
    outer_width, outer_height = core.width() * layout.dbu + 4, core.height() * layout.dbu + 4
    tap_area = .6 * (outer_width + outer_height - .6)
    tap_perimeter = 4 * (outer_width + outer_height - .6)
    schematic = (f'.subckt {name} PLUS MINUS SUB\n'
        f'Rpoly PLUS MINUS body rppd W={width_um:g}u L={length_um:g}u\n'
        f'Rtap SUB body ptap1 A={tap_area:.12g}p P={tap_perimeter:.12g}u\n.ends {name}\n')
    return cell, dict(model='rppd', requested_resistance_Ohm=resistance_Ohm,
        pcell_nominal_resistance_Ohm=nominal, width_um=width_um, length_um=length_um,
        pcell_body_current_limit=CbResCurrent(width_um * 1e-6, .001, 'rppdG2'),
        terminals=terminals, tap_area_um2=tap_area, tap_perimeter_um=tap_perimeter,
        bounding_area_um2=cell.bbox().area() * layout.dbu ** 2, schematic=schematic,
        spice_instance=f'Xpoly PLUS MINUS SUB rppd w={width_um:g}u l={length_um:g}u',
        scope='nominal PCell geometry; requires connectivity, DRC, process/parasitic and current/power qualification')
