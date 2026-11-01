"""Route compact clock macros from reviewed PDK cell circuits."""
from collections import defaultdict
from pathlib import Path
import re

import klayout.db as db

from clock_cell_library import definitions, library_path


def create_cells(pdk, circuit, cells, unused=(), compact_ports=False, order=None,
                 mirrored=(), track_groups=None, escape_width_um=.4):
    if escape_width_um not in (.24, .4):
        raise ValueError('unreviewed clock pin escape width')
    pdk = Path(pdk).resolve()
    base = pdk / 'ihp-sg13g2/libs.ref/sg13g2_stdcell'
    library = db.Layout()
    library.read(str(base / 'gds/sg13g2_stdcell.gds'))
    lef = (base / 'lef/sg13g2_stdcell.lef').read_text()
    layout = db.Layout()
    layout.dbu = .001
    top = layout.create_cell(circuit.splitlines()[0].split()[1])
    ports = circuit.splitlines()[0].split()[2:]
    instances = [line.split() for line in circuit.splitlines() if line.startswith('X')]
    if order is not None:
        if len(order) != len(instances) or set(order) != {fields[0] for fields in instances}:
            raise ValueError('clock-cell placement order must include every instance once')
        instances.sort(key=lambda fields: order.index(fields[0]))
    if not set(mirrored) <= {fields[0] for fields in instances}:
        raise ValueError('unknown mirrored clock cell')
    pins = defaultdict(list)
    components = []
    cursor = 0.

    def box(layer, values, datatype=0):
        shape = db.Box(*(round(value / layout.dbu) for value in values))
        top.shapes(layout.layer(layer, datatype)).insert(shape)
        return shape

    for fields in instances:
        name, model = fields[0], fields[-1]
        macro = re.findall(r'^MACRO ' + model + r'\n.*?^END ' + model + r'$', lef, re.M | re.S)
        if len(macro) != 1:
            raise ValueError('missing/ambiguous PDK clock-cell LEF: ' + model)
        size = re.findall(r'^  SIZE ([0-9.]+) BY ([0-9.]+) ;$', macro[0], re.M)
        if len(size) != 1 or float(size[0][1]) != 3.78:
            raise ValueError('changed standard-cell row geometry')
        if layout.cell(model) is None:
            original = library.cell(model)
            if original is None:
                raise ValueError('PDK clock cell missing from GDS')
            layout.create_cell(model).copy_tree(original)
        width = float(size[0][0])
        transform = (db.Trans(db.Trans.M90, round((cursor + width) / layout.dbu), 0)
                     if name in mirrored else db.Trans(round(cursor / layout.dbu), 0))
        top.insert(db.CellInstArray(layout.cell(model).cell_index(), transform))
        formal = cells[model][0].split()
        if len(fields[1:-1]) != len(formal):
            raise ValueError('clock-cell instance interface mismatch')
        for terminal, net in zip(formal, fields[1:-1]):
            if terminal in ('VDD', 'VSS') or net in unused:
                continue
            blocks = re.findall(r'^  PIN ' + terminal + r'\n.*?^  END ' + terminal + r'$', macro[0], re.M | re.S)
            if len(blocks) != 1 or set(re.findall(r'LAYER (\w+) ;', blocks[0])) != {'Metal1'}:
                raise ValueError('unreviewed standard-cell pin conductor')
            rectangles = [tuple(map(float, values)) for values in
                          re.findall(r'RECT ([0-9.-]+) ([0-9.-]+) ([0-9.-]+) ([0-9.-]+) ;', blocks[0])]
            if not rectangles:
                raise ValueError('standard-cell signal pin has no access rectangle')
            rectangle = max(rectangles, key=lambda r: min(r[2] - r[0], r[3] - r[1]))
            x = cursor + (rectangle[0] + rectangle[2]) / 2
            y = (rectangle[1] + rectangle[3]) / 2
            if (model, terminal) == ('sg13g2_a21oi_2', 'Y'):
                # The output and B1 pin centers are too close for parallel
                # Metal2 escapes. Use the output rectangle's left access.
                x = cursor + rectangle[0] + .03
            if model == 'sg13g2_a221oi_1':
                # The broad output spans its input columns. Escape from the
                # right vertical segment and the left part of A2's pin.
                if terminal == 'Y':
                    x, y = cursor + 3.67, 1.42
                elif terminal == 'A2':
                    x, y = cursor + 2.97, 1.71
            if model == 'sg13g2_nor2_2' and terminal == 'Y':
                # The widest output rectangle overlaps B's escape column.
                # Contact the separate right-hand vertical output conductor.
                x, y = cursor + 2.56, 1.4
            if model == 'sg13g2_nor2_2' and terminal == 'B':
                x = cursor + 1.85
            if model == 'sg13g2_inv_1':
                x = cursor + (1.04 if terminal == 'Y' else .4)
            if model == 'sg13g2_nand2_2':
                # Its broad Y shape spans both input columns. Access the
                # middle vertical segment and separate the input escapes.
                x = cursor + {'Y': 1.45, 'A': 2.075, 'B': .83}[terminal]
                if terminal == 'Y':
                    y = 2.56
                elif terminal == 'A':
                    y = 1.89
            if model == 'sg13g2_nand2_1':
                x = cursor + {'Y': .99, 'A': 1.605, 'B': .375}[terminal]
                if terminal == 'Y':
                    y = 2.6
            if model == 'sg13g2_o21ai_1':
                x = cursor + {'Y': 2.13, 'A1': .3, 'A2': .94, 'B1': 1.52}[terminal]
                if terminal == 'Y':
                    y = .895
                elif terminal == 'B1':
                    y = 1.88
            if model == 'sg13g2_and2_2' and terminal == 'A':
                x = cursor + .32
            if model == 'sg13g2_nor3_2':
                if terminal == 'Y':
                    x, y = cursor + 2.42, 1.
                elif terminal == 'B':
                    x = cursor + 1.8
            if name in mirrored:
                x = 2 * cursor + width - x
            # Snap via centers to the PDK's 5 nm grid before adding landings.
            x, y = round(x / .005) * .005, round(y / .005) * .005
            pins[net].append((x, y))
        components.append(dict(instance=name, model=model, origin_um=[cursor, 0],
                               mirrored_horizontally=name in mirrored,
                               connections=dict(zip(formal, fields[1:-1]))))
        cursor += width

    terminals, tracks = {}, {}
    nets = sorted(set(pins) - {'VDD', 'VSS'})
    nets += ['VDD', 'VSS']
    track_index = {net: index for index, net in enumerate(nets)}
    if track_groups is not None:
        grouped = [net for group in track_groups for net in group]
        if len(grouped) != len(set(grouped)) or set(grouped) != set(nets) - {'VSS'}:
            raise ValueError('clock track groups must cover every non-ground net once')
        # Disjoint left/right routes may share a height. Strict LVS and DRC
        # must still reject any overlapping or insufficiently spaced tracks.
        track_index = {net: index for index, group in enumerate(track_groups) for net in group}
    for index, net in enumerate(nets):
        track_y = -.8 if net == 'VSS' else (4.8 if compact_ports else 6.) + track_index[net] * .8
        points = list(pins[net])
        if net in ('VDD', 'VSS'):
            rail_y = 3.78 if net == 'VDD' else 0.
            box(8, (-2, rail_y - .22, cursor, rail_y + .22))
            box(10, (-1.84, rail_y - .2, -.16, rail_y + .2))
            for x in (-1.63, -1.21, -.79, -.37):
                box(19, (x - .095, rail_y - .095, x + .095, rail_y + .095))
            points.append((-1., rail_y))
        for x, y in points:
            if x >= 0:
                box(8, (x - .16, y - .16, x + .16, y + .16))
                box(19, (x - .095, y - .095, x + .095, y + .095))
            half = escape_width_um / 2
            box(10, (x - half, min(y, track_y), x + half, max(y, track_y)))
            landing_half = .18 if escape_width_um == .24 else .2
            for endpoint in (y, track_y):
                box(10, (x - landing_half, endpoint - landing_half,
                         x + landing_half, endpoint + landing_half))
            box(29, (x - .095, track_y - .095, x + .095, track_y + .095))
        if not points:
            raise ValueError('unconnected detector net: ' + net)
        # A single-pin clock/output need not drag a long unterminated track
        # across the macro. Keep its access beside the actual cell pin.
        left = (-3. if net in ports and not compact_ports
                else min(x for x, _ in points))
        right = max(x for x, _ in points) + .2
        track = box(30, (left - .2, track_y - .2, right, track_y + .2))
        tracks[net] = [track.left, track.bottom, track.right, track.top]
        if net in ports:
            box(30, (left - .1, track_y - .1, left + .1, track_y + .1), 2)
            top.shapes(layout.layer(30, 25)).insert(db.Text(net,
                db.Trans(round(left / layout.dbu), round(track_y / layout.dbu))))
            terminals[net] = dict(layer=30, x_um=left, y_um=track_y)
    if set(terminals) != set(ports):
        raise ValueError('detector external pin inventory changed')

    # KLayout's schematic reader consumes primitive MOS records, while ngspice
    # consumes the PDK's PSP subcircuits. Both retain the same D/G/S/B and W/L.
    schematic = [circuit]
    for line in definitions(library_path(pdk).read_text(), cells).splitlines():
        fields = line.split()
        if fields and fields[0].startswith('X'):
            dimensions = dict(item.split('=') for item in fields[6:])
            line = ' '.join(['M' + fields[0][1:], *fields[1:6],
                             'w=' + dimensions['w'], 'l=' + dimensions['l']])
        schematic.append(line)
    return layout, top, dict(ports=ports, terminals=terminals, tracks=tracks, components=components,
        normalized_device_count=sum(cells[fields[-1]][1] for fields in instances),
        schematic='\n'.join(schematic) + '\n', bounding_area_um2=top.bbox().area() * layout.dbu ** 2,
        scope='PDK-cell clock macro geometry candidate; requires independent DRC/LVS and extracted electrical tests')
