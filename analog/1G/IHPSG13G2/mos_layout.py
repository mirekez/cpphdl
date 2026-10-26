"""Physically connect every finger of the pinned IHP MOS PCells.

This is a four-terminal layout building block, not an analog place-and-router.
The substrate ring is retained as an explicit extracted tap device. Local
geometry and connectivity checks do not establish whole-chip metal density.
"""
import math
from pathlib import Path
import sys

import klayout.db as db


def device_library(pdk):
    python = Path(pdk).resolve() / 'ihp-sg13g2/libs.tech/klayout/python'
    for path in (python, python / 'pycell4klayout-api/source/python'):
        if str(path) not in sys.path:
            sys.path.insert(0, str(path))
    import sg13g2_pycell_lib  # noqa: F401; register the pinned PCells.
    library = db.Library.library_by_name('SG13_dev', 'sg13g2')
    if library is None:
        raise ValueError('prepared IHP device library is unavailable')
    return library


def append_mos(layout, pdk, name, width_um, length_um, fingers, polarity, domain):
    """Return a routed cell and its independent MOS/tap schematic contract.

    Width is total width, as in the PSP subcircuit. Unsupported geometries are
    rejected rather than silently quantized or exported with incomplete pins.
    A caller connects the four Metal2 terminals; labels never replace straps.
    """
    if (domain not in ('lv', 'hv') or polarity not in ('n', 'p')
            or not isinstance(fingers, int) or isinstance(fingers, bool) or fingers < 1
            or not all(math.isfinite(v) for v in (width_um, length_um))
            or not (1 if fingers == 1 and domain == 'lv' else 2) <= width_um / fingers <= 10
            or not (.45 if domain == 'hv' else .13) <= length_um <= 10
            or not math.isclose(layout.dbu, .001)
            or layout.cell(name) is not None):
        raise ValueError('unsupported routed MOS geometry, database unit or cell name')
    finger_width = width_um / fingers
    if any(abs(value * 100 - round(value * 100)) > 1e-8 for value in (finger_width, length_um)):
        raise ValueError('MOS dimensions must lie on the pinned PCell 10 nm grid')
    library = device_library(pdk)
    declaration = library.layout().pcell_declaration(polarity + 'mos' + ('HV' if domain == 'hv' else ''))
    if declaration is None:
        raise ValueError('pinned MOS PCell is unavailable')
    parameters = dict(w=f'{width_um:g}u', l=f'{length_um:g}u', ng=fingers)
    # Evaluate the unguarded PCell in a separate layout to obtain the ring's
    # specified dimensions without leaving an orphan device in the output GDS.
    probe = db.Layout()
    probe.dbu = layout.dbu
    core = probe.cell(probe.add_pcell_variant(library, declaration.id(), parameters)).bbox()
    index = layout.add_pcell_variant(library, declaration.id(), dict(parameters,
        guardRingType='psub' if polarity == 'n' else 'nwell', guardRingDistance='2u'))
    device = layout.cell(index)
    cell = layout.create_cell(name)
    cell.insert(db.CellInstArray(index, db.Trans()))
    m1, m2, via, poly, contact, oxide = [layout.layer(a, b) for a, b in (
        (8, 0), (10, 0), (19, 0), (5, 0), (6, 0), (44, 0))]

    def box(layer, x0, y0, x1, y1):
        cell.shapes(layer).insert(db.Box(*(round(v / layout.dbu) for v in (x0, y0, x1, y1))))

    def via_at(x, y):
        # Contact/via sizes and enclosures come from the pinned technology.
        # The landing is wider than the original 160 nm diffusion stripe.
        box(m1, x - .16, y - .16, x + .16, y + .16)
        box(via, x - .095, y - .095, x + .095, y + .095)
        box(m2, x - .16, y - .16, x + .16, y + .16)

    terminals = {}

    def pin(net, x, y):
        box(layout.layer(10, 2), x - .16, y - .16, x + .16, y + .16)
        cell.shapes(layout.layer(10, 25)).insert(db.Text(net, db.Trans(round(x / layout.dbu), round(y / layout.dbu))))
        terminals[net] = [x, y]

    def unique_boxes(layer, predicate):
        # The LV PMOS PCell repeats the first gate and the two pin-bearing
        # diffusion rectangles. Identical overlapping shapes are one conductor.
        boxes = {}
        for shape in device.each_shape(layer):
            if shape.is_box() and predicate(shape.bbox()):
                b = shape.bbox()
                boxes[(b.left, b.bottom, b.right, b.top)] = b
        return sorted(boxes.values(), key=lambda b: b.left)

    stripes = unique_boxes(m1, lambda b: b.height() == round(finger_width / layout.dbu))
    gates = unique_boxes(poly, lambda b: True)
    if len(stripes) != fingers + 1 or len(gates) != fingers:
        raise ValueError('pinned PCell diffusion/gate geometry changed')
    # A single narrow finger has independent diffusion stripes. Put its two
    # landings near opposite ends; the wider multi-finger buses need the
    # original inset to remain separated from their neighboring stripes.
    inset = .1 if finger_width < 2 else .45
    for parity, net, y in ((0, 'S', inset), (1, 'D', finger_width - inset)):
        xs = [s.center().x * layout.dbu for i, s in enumerate(stripes) if i % 2 == parity]
        for x in xs:
            via_at(x, y)
        box(m2, min(xs) - .2, y - .2, max(xs) + .2, y + .2)
        pin(net, xs[0], y)
    gate_xs = []
    for gate in gates:
        x = gate.center().x * layout.dbu
        gate_xs.append(x)
        box(poly, gate.left * layout.dbu, -.8, gate.right * layout.dbu, 0)
        # A 130 nm channel is narrower than a contacted poly landing. Widen
        # only the extension outside active; the intended channel stays intact.
        box(poly, x - .15, -.8, x + .15, -.4)
        box(contact, x - .08, -.68, x + .08, -.52)
        via_at(x, -.6)
    box(m2, min(gate_xs) - .2, -.8, max(gate_xs) + .2, -.4)
    pin('G', gate_xs[0], -.6)
    oxides = [s.bbox() for s in device.each_shape(oxide)]
    if len(oxides) != (1 if domain == 'hv' else 0):
        raise ValueError('pinned MOS oxide geometry changed')
    if oxides:
        ox = oxides[0]
        box(oxide, ox.left * layout.dbu, -1.14, ox.right * layout.dbu, ox.top * layout.dbu)
    # Select the vertical ring strip, never the bottom strip's centre: the
    # latter can put the body via onto an unrelated source/drain finger.
    ring = min((s.bbox() for s in device.each_shape(m1)
        if s.is_box() and s.bbox().height() > s.bbox().width()), key=lambda b: b.left)
    body_x, body_y = ring.center().x * layout.dbu, finger_width / 2
    via_at(body_x, body_y)
    # An isolated terminal also needs the 0.144 um² Metal2 minimum area.
    box(m2, body_x - .2, body_y - .2, body_x + .2, body_y + .2)
    pin('B', body_x, body_y)
    # PCell ring: 300 nm active width, outer dimensions equal the original
    # unguarded PCell extent plus 2 um clearance on every side. Keep its contact as a
    # real schematic element instead of disabling tap extraction in LVS.
    outer_width = core.width() * layout.dbu + 4
    outer_height = core.height() * layout.dbu + 4
    tap_area = .6 * (outer_width + outer_height - .6)
    tap_perimeter = 4 * (outer_width + outer_height - .6)
    model = 'sg13_' + domain + '_' + polarity + 'mos'
    tap = 'ptap1' if polarity == 'n' else 'ntap1'
    schematic = (f'.SUBCKT {name} D G S B\n'
        f'M0 D G S body {model} W={width_um:g}u L={length_um:g}u\n'
        f'Rtap B body {tap} A={tap_area:.12g}p P={tap_perimeter:.12g}u\n'
        f'.ENDS {name}\n')
    return cell, dict(model=model, total_width_um=width_um,
        length_um=length_um, fingers=fingers, finger_width_um=finger_width,
        terminals_um=terminals, bbox_um=[v * layout.dbu for v in (
            cell.bbox().left, cell.bbox().bottom, cell.bbox().right, cell.bbox().top)],
        source_straps=(fingers // 2 + 1), drain_straps=(fingers + 1) // 2,
        gate_contacts=fingers, tap_area_um2=tap_area, tap_perimeter_um=tap_perimeter,
        bounding_area_um2=cell.bbox().area() * layout.dbu ** 2,
        schematic=schematic, scope='four-terminal transistor macro; density and extracted performance require integration')


def append_hv_nmos(layout, pdk, name, width_um, length_um, fingers):
    return append_mos(layout, pdk, name, width_um, length_um, fingers, 'n', 'hv')


def append_hv_pmos(layout, pdk, name, width_um, length_um, fingers):
    return append_mos(layout, pdk, name, width_um, length_um, fingers, 'p', 'hv')


def append_lv_nmos(layout, pdk, name, width_um, length_um, fingers):
    return append_mos(layout, pdk, name, width_um, length_um, fingers, 'n', 'lv')


def append_lv_pmos(layout, pdk, name, width_um, length_um, fingers):
    return append_mos(layout, pdk, name, width_um, length_um, fingers, 'p', 'lv')
