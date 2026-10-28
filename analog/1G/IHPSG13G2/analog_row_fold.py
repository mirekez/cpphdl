"""Fold a connected analog row at device gutters and reroute its net buses.

Device geometry is never cut or scaled. Each net retains its own Metal5 row
track; nets spanning rows connect through separate Metal4 spines outside the
device area. The resulting GDS requires fresh LVS, DRC and extraction.
"""
from bisect import bisect_right
from collections import defaultdict
import math

import klayout.db as db


def fold_row(layout, top, info, width_um=1800):
    if not math.isfinite(width_um) or width_um < 200:
        raise ValueError('invalid analog row width')
    scale = 1 / layout.dbu
    limit = round(width_um * scale)
    bounds = top.bbox()
    cuts = [0]
    for part in info['components']:
        cell = layout.cell(part['name'])
        dx = round(part['origin_um'][0] * scale)
        start = dx + cell.bbox().left - round(5 * scale)
        end = dx + cell.bbox().right + round(5 * scale)
        if end - start > limit:
            raise ValueError('an indivisible analog component exceeds the row width')
        if end - cuts[-1] > limit:
            if start <= cuts[-1]:
                raise ValueError('missing device gutter for row folding')
            cuts.append(start)
    cuts.append(max(end, bounds.right + 1))
    if len(cuts) <= 2:
        return layout, top, info

    def row_for(x):
        row = bisect_right(cuts, x) - 1
        if not 0 <= row < len(cuts) - 1:
            raise ValueError('geometry outside row partition')
        return row

    # Recover the actual bus level from the device-column vias. All bus vias
    # lie below y=0; within-device capacitor escapes lie above it.
    via_y = defaultdict(set)
    for shape in top.each_shape(layout.layer(66, 0)):
        point = shape.bbox().center()
        if point.y < 0:
            via_y[point.x].add(point.y)
    points, bus_y = defaultdict(list), {}
    for escape in info['escapes']:
        x, y = (round(v * scale) for v in escape['location_um'])
        points[escape['net']].append((x, y))
    for net, endpoints in points.items():
        levels = set.intersection(*(via_y[x] for x, _ in endpoints))
        if len(levels) != 1:
            raise ValueError('ambiguous analog row bus: ' + net)
        bus_y[net] = levels.pop()
    if len(set(bus_y.values())) != len(bus_y):
        raise ValueError('two analog nets share a bus level')

    power = {'VDD', 'VSS', 'VLOGIC'}
    spine_x, cursor = {}, round(2 * scale)
    for net in sorted(points):
        if len({row_for(x) for x, _ in points[net]}) > 1:
            half = round((2.5 if net in power else .2) * scale)
            cursor += half
            spine_x[net] = cursor
            cursor += half + round(.8 * scale)
    offset = cursor + round(2 * scale)
    pitch = bounds.height() + round(10 * scale)
    moves = [db.Trans(offset - cuts[row], row * pitch) for row in range(len(cuts) - 1)]

    name = top.name
    top.name = name + '_unfolded'
    folded = layout.create_cell(name)
    for instance in top.each_inst():
        if instance.na > 1 or instance.nb > 1:
            raise ValueError('unexpected top-level analog instance array')
        box = instance.bbox()
        row = row_for(box.left)
        if box.right >= cuts[row + 1]:
            raise ValueError('row boundary cuts an analog device')
        folded.insert(db.CellInstArray(instance.cell_index, moves[row] * instance.trans))

    for layer in layout.layer_indexes():
        region = db.Region()
        for shape in top.each_shape(layer):
            if shape.is_text():
                text = shape.text
                folded.shapes(layer).insert(text.transformed(moves[row_for(text.trans.disp.x)]))
            elif shape.is_box():
                # Replace global buses and their landing enclosures. Keep all
                # column vias, devices' escapes and physical port markers.
                if layer == layout.layer(67, 0) and shape.box.center().y in bus_y.values():
                    continue
                region.insert(shape.box)
            else:
                raise ValueError('unsupported top-level analog routing shape')
        for row, move in enumerate(moves):
            window = db.Region(db.Box(cuts[row], bounds.bottom - 1, cuts[row + 1], bounds.top + 1))
            folded.shapes(layer).insert((region & window).transformed(move))

    def box(layer, x0, y0, x1, y1):
        folded.shapes(layout.layer(layer, 0)).insert(db.Box(x0, y0, x1, y1))

    landing, cut = round(.2 * scale), round(.095 * scale)
    spines, row_buses = {}, defaultdict(list)
    for net, endpoints in points.items():
        half = round((2.5 if net in power else .12) * scale)
        rows = defaultdict(list)
        for x, _ in endpoints:
            row = row_for(x)
            rows[row].append(x + moves[row].disp.x)
        for row, xs in rows.items():
            y = bus_y[net] + moves[row].disp.y
            taps = xs + ([spine_x[net]] if net in spine_x else [])
            row_buses[net].append(dict(row=row, y_um=y / scale,
                x_min_um=min(taps) / scale, x_max_um=max(taps) / scale, width_um=2 * half / scale))
            box(67, min(taps) - landing, y - half, max(taps) + landing, y + half)
            for x in taps:
                box(67, x - landing, y - landing, x + landing, y + landing)
            if net in spine_x:
                x = spine_x[net]
                if net in power:
                    box(67, x - half, y - half, x + half, y + half)
                # The supply crossing uses an actual via array. Signal
                # crossings retain one legal via and its metal enclosures.
                shifts = (-1.2, -.6, 0, .6, 1.2) if net in power else (0,)
                for dx in shifts:
                    for dy in shifts:
                        vx, vy = x + round(dx * scale), y + round(dy * scale)
                        box(66, vx - cut, vy - cut, vx + cut, vy + cut)
        if net in spine_x:
            x = spine_x[net]
            ys = [bus_y[net] + moves[row].disp.y for row in rows]
            vertical_half = round((2.5 if net in power else .2) * scale)
            box(50, x - vertical_half, min(ys) - vertical_half, x + vertical_half, max(ys) + vertical_half)
            spines[net] = dict(x_um=x / scale, y_min_um=min(ys) / scale,
                              y_max_um=max(ys) / scale, width_um=2 * vertical_half / scale)

    for item in info['components']:
        x, y = item['origin_um']
        move = moves[row_for(round(x * scale) + layout.cell(item['name']).bbox().left)]
        item['origin_um'] = [x + move.disp.x / scale, y + move.disp.y / scale]
    for item in info['escapes']:
        point = db.Point(*(round(v * scale) for v in item['location_um']))
        point = moves[row_for(point.x)] * point
        item['location_um'] = [point.x / scale, point.y / scale]
    for item in info['terminals'].values():
        point = db.Point(round(item['x_um'] * scale), round(item['y_um'] * scale))
        point = moves[row_for(point.x)] * point
        item.update(x_um=point.x / scale, y_um=point.y / scale)
    info['folded_rows'] = dict(rows=len(moves), requested_width_um=width_um,
        source_cuts_um=[x / scale for x in cuts], pitch_um=pitch / scale,
        spine_width_um=offset / scale, inter_row_nets=len(spine_x), spines=spines,
        row_buses=dict(row_buses))
    if 'logic_supply_bus' in info:
        info['logic_supply_bus'] = dict(width_um=5, tracks=row_buses['VLOGIC'], spine=spines.get('VLOGIC'))
    info.update(width_um=folded.bbox().width() / scale, height_um=folded.bbox().height() / scale,
                bounding_area_um2=folded.bbox().area() / scale ** 2)
    layout.delete_cell(top.cell_index())
    return layout, folded, info
