"""An actual P-diffusion/N-well antenna diode for a high-impedance net."""
import math

import klayout.db as db
from mos_layout import device_library


def append_antenna(layout, pdk, name):
    if not math.isclose(layout.dbu, .001) or layout.cell(name) is not None:
        raise ValueError('requires unused cell name and 1 nm database unit')
    library = device_library(pdk)
    declaration = library.layout().pcell_declaration('dpantenna')
    if declaration is None:
        raise ValueError('missing process antenna-diode PCell')
    width = length = .78
    parameters = dict(w='0.78u', l='0.78u', Calculate='a', addRecLayer='t')
    probe = db.Layout()
    probe.dbu = layout.dbu
    core = probe.cell(probe.add_pcell_variant(library, declaration.id(), parameters)).bbox()
    index = layout.add_pcell_variant(library, declaration.id(), dict(parameters,
        guardRingType='nwell', guardRingDistance='2u'))
    device = layout.cell(index)
    cell = layout.create_cell(name)
    cell.insert(db.CellInstArray(index, db.Trans()))
    terminals = {}

    def contact(net, x, y):
        for layer, half in ((8, .16), (19, .095), (10, .2)):
            cell.shapes(layout.layer(layer, 0)).insert(db.Box(*(round(v / layout.dbu)
                for v in (x - half, y - half, x + half, y + half))))
        cell.shapes(layout.layer(10, 2)).insert(db.Box(*(round(v / layout.dbu)
            for v in (x - .16, y - .16, x + .16, y + .16))))
        cell.shapes(layout.layer(10, 25)).insert(db.Text(net, db.Trans(round(x / layout.dbu), round(y / layout.dbu))))
        terminals[net] = dict(layer=10, x_um=x, y_um=y)

    # This PCell omits pin-purpose shapes and repeats its drawing rectangle.
    # Select the unique conductor inside the intended P diffusion, excluding
    # the surrounding well ring; labels never substitute for a contact.
    diffusion = db.Box(0, 0, round(width / layout.dbu), round(length / layout.dbu))
    contacts = list({str(s.bbox()): s.bbox() for s in device.each_shape(layout.layer(8, 0))
                     if s.is_box() and (diffusion & s.bbox()) == s.bbox()}.values())
    if len(contacts) != 1:
        raise ValueError('changed antenna cathode contact')
    # The PCell's Metal1 rectangle ends at the contact-array boundary. Supply
    # the required 60 nm enclosure around every outer contact, not just the
    # centre via used by the external Metal2 route.
    cell.shapes(layout.layer(8, 0)).insert(contacts[0].enlarged(60))
    anode = contacts[0].center()
    contact('PLUS', anode.x * layout.dbu, anode.y * layout.dbu)
    ring = min((s.bbox() for s in device.each_shape(layout.layer(8, 0))
                if s.is_box() and s.bbox().height() > s.bbox().width()), key=lambda p: p.left)
    contact('MINUS', ring.center().x * layout.dbu, ring.center().y * layout.dbu)
    outer_width, outer_height = core.width() * layout.dbu + 4, core.height() * layout.dbu + 4
    tap_area = .6 * (outer_width + outer_height - .6)
    tap_perimeter = 4 * (outer_width + outer_height - .6)
    schematic = (f'.subckt {name} PLUS MINUS\n'
        f'Dprotect PLUS body dpantenna W={width:g}u L={length:g}u\n'
        f'Rtap MINUS body ntap1 A={tap_area:.12g}p P={tap_perimeter:.12g}u\n.ends {name}\n')
    return cell, dict(model='dpantenna', guard_terminal='MINUS', width_um=width, length_um=length, terminals=terminals,
        schematic=schematic, bounding_area_um2=cell.bbox().area() * layout.dbu ** 2,
        scope='physical antenna protection; its process-model capacitance and leakage must remain in electrical tests')
