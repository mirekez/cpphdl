"""Compact a connected device row using only the nets present in each row.

Primitive geometry and the circuit graph stay unchanged. Device escapes above
y=0 are retained; global buses are replaced with row-local buses and shared
spines. This is physical routing and requires fresh LVS, DRC and extraction.
"""
from bisect import bisect_right
from collections import defaultdict
import math

import klayout.db as db


def compact_rows(layout, top, info, width_um=600, row_gap_um=10, keep_together_nets=()):
    if (not math.isfinite(width_um) or width_um < 200 or
            not math.isfinite(row_gap_um) or row_gap_um < 10):
        raise ValueError('invalid compact-row dimensions')
    unit = 1 / layout.dbu
    limit = round(width_um * unit)
    source_bounds = top.bbox()
    if not info['components'] or not info['escapes']:
        raise ValueError('compaction needs devices and connected escapes')
    parts = []
    for part in info['components']:
        cell = layout.cell(part['name'])
        x = round(part['origin_um'][0] * unit)
        start, end = x + cell.bbox().left - round(5 * unit), x + cell.bbox().right + round(5 * unit)
        if end - start > limit:
            raise ValueError('indivisible device exceeds row width')
        parts.append((start, end, part))
    protected = []
    for net in keep_together_nets:
        members = [(start, end) for start, end, part in parts if net in part.get('connections', {}).values()]
        if not members:
            raise ValueError('protected net has no devices: ' + net)
        protected.append((min(p[0] for p in members), max(p[1] for p in members)))
    merged = []
    for start, end in sorted(protected):
        if merged and start <= merged[-1][1]:
            merged[-1] = (merged[-1][0], max(end, merged[-1][1]))
        else:
            merged.append((start, end))
    if any(end - start > limit for start, end in merged):
        raise ValueError('protected net group exceeds row width; improve device placement')
    cuts = [0]
    for start, end, part in parts:
        planned_end = max([end] + [right for left, right in merged if left <= start < right])
        if planned_end - cuts[-1] > limit:
            if start <= cuts[-1]:
                raise ValueError('missing device gutter')
            cuts.append(start)
    cuts.append(max(end, source_bounds.right + 1))

    def row_for(x):
        row = bisect_right(cuts, x) - 1
        if not 0 <= row < len(cuts) - 1:
            raise ValueError('route lies outside device partition')
        return row

    endpoints = defaultdict(lambda: defaultdict(list))
    for escape in info['escapes']:
        x, y = [round(v * unit) for v in escape['location_um']]
        if y < 0:
            raise ValueError('device escape is below the routing boundary')
        endpoints[escape['net']][row_for(x)].append(db.Point(x, y))
    power = {'VDD', 'VSS', 'VLOGIC'}
    spine_x, cursor = {}, round(2 * unit)
    for net, rows in sorted(endpoints.items()):
        if len(rows) > 1:
            half = round((2.5 if net in power else .2) * unit)
            cursor += half
            spine_x[net] = cursor
            cursor += half + round(.8 * unit)
    offset = cursor + round(2 * unit)
    tracks, depths = [], []
    for row in range(len(cuts) - 1):
        level, levels = -round(unit), {}
        for net in sorted(n for n, rows in endpoints.items() if row in rows):
            half = round((2.5 if net in power else .12) * unit)
            level -= round(.76 * unit) + half
            levels[net] = level
            level -= half
        tracks.append(levels)
        depths.append(-level)

    # Keep actual device/escape extents, rather than giving every row the
    # tallest device's height and every net in the original strip.
    regions, heights = {}, [0] * (len(cuts) - 1)
    for layer in layout.layer_indices():
        region = db.Region()
        for shape in top.shapes(layer).each():
            if shape.is_text():
                if shape.text.trans.disp.y >= 0:
                    raise ValueError('unexpected device-area top label')
            elif shape.is_box():
                if shape.box.top > 0:
                    region.insert(shape.box & db.Box(source_bounds.left, 0, source_bounds.right, source_bounds.top))
            else:
                raise ValueError('unsupported original escape shape')
        regions[layer] = region
        for row in range(len(heights)):
            window = db.Region(db.Box(cuts[row], 0, cuts[row + 1], source_bounds.top + 1))
            piece = region & window
            if not piece.is_empty():
                heights[row] = max(heights[row], piece.bbox().top)
    for inst in top.each_inst():
        bounds = inst.bbox()
        row = row_for(bounds.left)
        if bounds.right >= cuts[row + 1]:
            raise ValueError('partition cuts a primitive')
        heights[row] = max(heights[row], bounds.top)
    moves, bottom = [], 0
    for row, height in enumerate(heights):
        base = bottom + depths[row]
        moves.append(db.Trans(offset - cuts[row], base))
        bottom = base + height + round(row_gap_um * unit)
    name = top.name
    top.name = name + '_strip'
    compact = layout.create_cell(name)
    for inst in top.each_inst():
        if inst.na > 1 or inst.nb > 1:
            raise ValueError('unexpected primitive array')
        compact.insert(db.CellInstArray(inst.cell_index, moves[row_for(inst.bbox().left)] * inst.trans))
    for layer, region in regions.items():
        for row, move in enumerate(moves):
            window = db.Region(db.Box(cuts[row], 0, cuts[row + 1], source_bounds.top + 1))
            compact.shapes(layer).insert((region & window).transformed(move))

    def box(layer, x0, y0, x1, y1):
        compact.shapes(layout.layer(layer, 0)).insert(db.Box(x0, y0, x1, y1))

    land, cut = round(.2 * unit), round(.095 * unit)

    def via(x, y):
        for layer in (50, 67):
            box(layer, x - land, y - land, x + land, y + land)
        box(66, x - cut, y - cut, x + cut, y + cut)

    terminals, row_buses = {}, defaultdict(list)
    for net, rows in sorted(endpoints.items()):
        levels = []
        half = round((2.5 if net in power else .12) * unit)
        for row, points in sorted(rows.items()):
            moved = [moves[row] * point for point in points]
            y = tracks[row][net] + moves[row].disp.y
            xs = [point.x for point in moved] + ([spine_x[net]] if net in spine_x else [])
            box(67, min(xs) - land, y - half, max(xs) + land, y + half)
            for point in moved:
                box(50, point.x - land, y - land, point.x + land, point.y + land)
                via(point.x, y)
            if net in spine_x:
                x = spine_x[net]
                if net in power:
                    # Supply trunks require distributed cuts, not a single
                    # signal-sized bottleneck between otherwise wide wires.
                    box(67, x - half, y - half, x + half, y + half)
                    for dx in (-1.2, -.6, 0, .6, 1.2):
                        for dy in (-1.2, -.6, 0, .6, 1.2):
                            via(x + round(dx * unit), y + round(dy * unit))
                else:
                    via(x, y)
            levels.append(y)
            row_buses[net].append(dict(row=row, y_um=y / unit, x_min_um=min(xs) / unit,
                                      x_max_um=max(xs) / unit))
            if net in info['terminals'] and net not in terminals:
                x = min(xs)
                compact.shapes(layout.layer(67, 25)).insert(db.Text(net, db.Trans(x, y)))
                compact.shapes(layout.layer(67, 2)).insert(db.Box(x - 160, y - 160, x + 160, y + 160))
                terminals[net] = dict(layer=67, x_um=x / unit, y_um=y / unit)
        if net in spine_x:
            width = round((2.5 if net in power else .2) * unit)
            x = spine_x[net]
            box(50, x - width, min(levels) - width, x + width, max(levels) + width)
    if set(terminals) != set(info['terminals']):
        raise ValueError('compaction changed external ports')
    for part in info['components']:
        x, y = [round(v * unit) for v in part['origin_um']]
        row = row_for(x + layout.cell(part['name']).bbox().left)
        point = moves[row] * db.Point(x, y)
        part['origin_um'] = [point.x / unit, point.y / unit]
    for escape in info['escapes']:
        point = db.Point(*[round(v * unit) for v in escape['location_um']])
        point = moves[row_for(point.x)] * point
        escape['location_um'] = [point.x / unit, point.y / unit]
    info['terminals'] = terminals
    info['compaction'] = dict(rows=len(moves), requested_width_um=width_um,
        keep_together_nets=list(keep_together_nets),
        source_area_um2=source_bounds.area() / unit ** 2, row_gap_um=row_gap_um,
        row_bus_counts=[len(t) for t in tracks], row_depths_um=[d / unit for d in depths],
        row_device_heights_um=[h / unit for h in heights], row_buses=dict(row_buses))
    info.update(width_um=compact.bbox().width() / unit, height_um=compact.bbox().height() / unit,
                bounding_area_um2=compact.bbox().area() / unit ** 2)
    layout.delete_cell(top.cell_index())
    return layout, compact, info
