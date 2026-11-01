"""Parallel contacted-poly pullup with distributed line-current connections."""
import math

import klayout.db as db

from resistor_layout import append_rppd


def append_transmit_pullup(layout, pdk, name):
    """Realize 50 ohms as eight 400-ohm, 12.5 um-wide rppd sections.

    The 100 um total body width and distributed contacts support a subsequent
    current-density assessment. This constructor does not establish thermal,
    electromigration, mismatch or complete driver qualification.
    """
    if not math.isclose(layout.dbu, .001) or layout.cell(name) is not None:
        raise ValueError('unsupported pullup database unit or cell name')
    top = layout.create_cell(name)
    units = []
    cursor = 0.

    def box(layer, x0, y0, x1, y1):
        top.shapes(layout.layer(layer, 0)).insert(db.Box(*(round(v / layout.dbu)
            for v in (x0, y0, x1, y1))))

    landings = {net: [] for net in ('PLUS', 'MINUS', 'SUB')}
    for index in range(8):
        cell, geometry = append_rppd(layout, pdk, name + 'Section' + str(index), 400, 12.5)
        bounds = cell.bbox()
        dx = cursor - bounds.left * layout.dbu
        # Only the assembled macro declares external ports. Remove the two
        # original centre vias before replacing them with legal parallel rows.
        cell.shapes(layout.layer(10, 2)).clear()
        cell.shapes(layout.layer(10, 25)).clear()
        centres = {db.Point(round(geometry['terminals'][net]['x_um'] / layout.dbu),
                            round(geometry['terminals'][net]['y_um'] / layout.dbu))
                   for net in ('PLUS', 'MINUS')}
        for shape in list(cell.each_shape(layout.layer(19, 0))):
            if shape.bbox().center() in centres:
                shape.delete()
        top.insert(db.CellInstArray(cell.cell_index(), db.Trans(round(dx / layout.dbu), 0)))
        for net, terminal in geometry['terminals'].items():
            x, y = terminal['x_um'] + dx, terminal['y_um']
            landings[net].append((x, y))
            if net == 'SUB':
                continue
            # The wide end bar removes notches between individual landings.
            # All original poly/contact shapes remain unchanged in the PCell.
            half = geometry['width_um'] / 2 + .2
            box(8, x - half, y - .4, x + half, y + .4)
            for layer in (10, 30, 50):
                box(layer, x - half, y - .42, x + half, y + .42)
            box(67, x - half, y - .74, x + half, y + .74)
            for column in range(-7, 8):
                via_x = x + column * .84
                for row in (y - .21, y + .21):
                    for layer in (19, 29, 49, 66):
                        box(layer, via_x - .095, row - .095, via_x + .095, row + .095)
                for row in (y - .42, y + .42):
                    box(125, via_x - .21, row - .21, via_x + .21, row + .21)
        units.append(dict(geometry=geometry, origin_um=[dx, 0]))
        cursor = dx + bounds.right * layout.dbu + 2

    terminals = {}
    for net, points in landings.items():
        ys = {round(y / layout.dbu) for _, y in points}
        if len(ys) != 1:
            raise ValueError('unaligned pullup terminals')
        y = points[0][1]
        if net == 'SUB':
            layer, half, x = 10, .2, min(p[0] for p in points)
            left, right = min(p[0] for p in points) - .2, max(p[0] for p in points) + .2
        else:
            layer, half = 126, 1.05
            left, right = min(p[0] for p in points) - 6.51, max(p[0] for p in points) + 6.51
            x = round((left + right) / (2 * layout.dbu)) * layout.dbu
        box(layer, left, y - half, right, y + half)
        top.shapes(layout.layer(layer, 2)).insert(db.Box(*(round(v / layout.dbu)
            for v in (x - .2, y - .2, x + .2, y + .2))))
        top.shapes(layout.layer(layer, 25)).insert(db.Text(net,
            db.Trans(round(x / layout.dbu), round(y / layout.dbu))))
        terminals[net] = dict(layer=layer, x_um=x, y_um=y)

    schematic = f'.subckt {name} PLUS MINUS SUB\n'
    process = '.subckt PhysicalPullup PLUS MINUS SUB\n'
    for index, unit in enumerate(units):
        g = unit['geometry']
        schematic += (f'R{index} PLUS MINUS body rppd W={g["width_um"]:g}u L={g["length_um"]:g}u\n'
                      f'Rtap{index} SUB body ptap1 A={g["tap_area_um2"]:.12g}p P={g["tap_perimeter_um"]:.12g}u\n')
        process += f'X{index} PLUS MINUS SUB rppd w={g["width_um"]:g}u l={g["length_um"]:g}u\n'
    schematic += f'.ends {name}\n'
    process += '.ends PhysicalPullup\n'
    nominal = 1 / sum(1 / u['geometry']['pcell_nominal_resistance_Ohm'] for u in units)
    return top, dict(model='parallel_rppd', sections=units, total_width_um=100,
        nominal_resistance_Ohm=nominal, terminals=terminals, schematic=schematic,
        process_netlist=process, vias_per_section_per_terminal_per_transition=30,
        bbox_um=[v * layout.dbu for v in (top.bbox().left, top.bbox().bottom,
                                        top.bbox().right, top.bbox().top)],
        bounding_area_um2=top.bbox().area() * layout.dbu ** 2,
        scope='50-ohm parallel geometry and current collection; requires LVS, DRC, extracted electrical and thermal/EM verification')
