"""Parallel HV NMOS banks with explicit gate, diffusion and substrate straps."""
import math

import klayout.db as db

from mos_layout import append_hv_nmos


def append_hv_nmos_bank(layout, pdk, name, width_um, length_um, fingers):
    """Preserve total width without exceeding the PCell's 100-finger limit.

    Five-micron fingers keep guarded active density bounded. Physical gaps
    separate guard rings; all four terminals have real continuous conductors.
    Drain and source collect through distributed via stacks into TopMetal1.
    Extracted electrical/EM checks remain necessary for a qualified transmitter.
    """
    if (isinstance(fingers, bool) or not isinstance(fingers, int) or
            not 1 <= fingers <= 1000 or not math.isfinite(width_um) or
            not math.isclose(width_um, fingers * 5, rel_tol=0, abs_tol=1e-9) or
            not math.isclose(layout.dbu, .001) or layout.cell(name) is not None):
        raise ValueError('unsupported five-micron HV NMOS bank geometry')
    top = layout.create_cell(name)
    banks = []
    cursor = 0.
    remaining = fingers
    while remaining:
        count = min(100, remaining)
        cell, geometry = append_hv_nmos(layout, pdk, name + 'Unit' + str(len(banks)),
                                       count * 5, length_um, count)
        # Internal units are not external port locations. In particular a
        # lower-metal D/S pin would let extraction inject current below the
        # intended TopMetal1 terminal and bypass part of its via resistance.
        cell.shapes(layout.layer(10, 2)).clear()
        cell.shapes(layout.layer(10, 25)).clear()
        bounds = cell.bbox()
        dx = cursor - bounds.left * layout.dbu
        top.insert(db.CellInstArray(cell.cell_index(), db.Trans(round(dx / layout.dbu), 0)))
        banks.append(dict(origin_um=[dx, 0], geometry=geometry))
        # Guarded cells are separated by 2 um of real interconnect corridor.
        cursor = dx + bounds.right * layout.dbu + 2
        remaining -= count
    physical_terminals = {}
    for terminal in ('D', 'G', 'S', 'B'):
        points = [(unit['origin_um'][0] + unit['geometry']['terminals_um'][terminal][0],
                   unit['geometry']['terminals_um'][terminal][1]) for unit in banks]
        ys = {round(y / layout.dbu) for _, y in points}
        if len(ys) != 1:
            raise ValueError('unaligned parallel MOS terminals')
        x0, x1 = min(x for x, _ in points), max(x for x, _ in points)
        y = points[0][1]
        top.shapes(layout.layer(10, 0)).insert(db.Box(*(round(v / layout.dbu)
            for v in (x0 - .2, y - .2, x1 + .2, y + .2))))
        if terminal in ('G', 'B'):
            top.shapes(layout.layer(10, 2)).insert(db.Box(*(round(v / layout.dbu)
                for v in (x0 - .16, y - .16, x0 + .16, y + .16))))
            top.shapes(layout.layer(10, 25)).insert(db.Text(terminal,
                db.Trans(round(x0 / layout.dbu), round(y / layout.dbu))))
        physical_terminals[terminal] = dict(layer=10, x_um=x0, y_um=y)
    collectors = {}

    def box(layer, x0, y0, x1, y1):
        top.shapes(layout.layer(layer, 0)).insert(db.Box(*(round(v / layout.dbu)
            for v in (x0, y0, x1, y1))))

    for terminal in ('D', 'S'):
        points = []
        for index, unit in enumerate(banks):
            cell = layout.cell(name + 'Unit' + str(index))
            y = unit['geometry']['terminals_um'][terminal][1]
            # Every diffusion stripe has its own Via1 landing. Lift each
            # landing, so the thin Metal2 bus does not collect the entire
            # device current before reaching the upper-metal rail.
            for shape in cell.each_shape(layout.layer(19, 0)):
                center = shape.bbox().center()
                if center.y == round(y / layout.dbu):
                    points.append((center.x * layout.dbu + unit['origin_um'][0], y))
        expected = sum(unit['geometry']['drain_straps' if terminal == 'D' else 'source_straps']
                       for unit in banks)
        if len(points) != expected:
            raise ValueError('incomplete diffusion collector inventory')
        y = points[0][1]
        xs = [x for x, _ in points]
        inner_y = y + (.42 if terminal == 'S' else -.42)
        for x in xs:
            # Add a second Via1 inside the same diffusion stripe, then use
            # parallel cuts at every inter-metal transition. A single chain
            # per stripe produced excessive DC degeneration in the TX cells.
            box(8, x - .16, min(y, inner_y) - .16, x + .16, max(y, inner_y) + .16)
            box(19, x - .095, inner_y - .095, x + .095, inner_y + .095)
            for layer in (10, 30, 50):
                box(layer, x - .2, min(y, inner_y) - .2, x + .2, max(y, inner_y) + .2)
            for cut in (29, 49, 66):
                for row in (y, inner_y):
                    box(cut, x - .095, row - .095, x + .095, row + .095)
            box(67, x - .32, y - .74, x + .32, y + .74)
            for row in (y - .42, y + .42):
                box(125, x - .21, row - .21, x + .21, row + .21)
        # TopVia1 needs 0.42 um TopMetal1 enclosure:
        # row offset .42 + half cut .21 + enclosure .42 = 1.05 um per side.
        box(126, min(xs) - 1.05, y - 1.05, max(xs) + 1.05, y + 1.05)
        # Inject current at the centre of the rail. Feeding the whole long
        # bank from one end adds avoidable source degeneration and IR drop.
        x = round((min(xs) + max(xs)) / (2 * layout.dbu)) * layout.dbu
        top.shapes(layout.layer(126, 2)).insert(db.Box(*(round(v / layout.dbu)
            for v in (x - .4, y - .4, x + .4, y + .4))))
        top.shapes(layout.layer(126, 25)).insert(db.Text(terminal,
            db.Trans(round(x / layout.dbu), round(y / layout.dbu))))
        physical_terminals[terminal] = dict(layer=126, x_um=x, y_um=y)
        collectors[terminal] = dict(layer=126, width_um=2.1,
            diffusion_stripes=len(points), parallel_vias_per_transition=2,
            diffusion_landings_um=points)
    # An independent total-width transistor checks the aggregate of all
    # physical fingers. Guard contacts remain explicit parallel substrate taps.
    schematic = (f'.SUBCKT {name} D G S B\n'
                 f'M0 D G S body sg13_hv_nmos W={width_um:g}u L={length_um:g}u\n')
    for index, unit in enumerate(banks):
        geometry = unit['geometry']
        schematic += (f'Rtap{index} B body ptap1 A={geometry["tap_area_um2"]:.12g}p '
                      f'P={geometry["tap_perimeter_um"]:.12g}u\n')
    schematic += f'.ENDS {name}\n'
    return top, dict(model='sg13_hv_nmos', total_width_um=width_um,
        length_um=length_um, fingers=fingers, finger_width_um=5,
        banks=banks, terminals=physical_terminals, collectors=collectors, schematic=schematic,
        bbox_um=[v * layout.dbu for v in (top.bbox().left, top.bbox().bottom,
                                        top.bbox().right, top.bbox().top)],
        bounding_area_um2=top.bbox().area() * layout.dbu ** 2,
        scope='parallel geometry with distributed TopMetal1 collectors; current sharing, extracted performance and chip density unqualified')
