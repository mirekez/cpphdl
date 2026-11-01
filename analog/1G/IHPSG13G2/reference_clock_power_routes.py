"""Short supply branches for the folded physical reference-clock composition.

These are actual conductors and via cuts. The independent schematic remains
unchanged; callers must run fresh DRC, LVS, extraction and electrical checks.
"""
import math

import klayout.db as db


def add_clock_power_routes(layout, top, info):
    folded = info.get('folded_rows')
    if not folded or folded['rows'] != 2:
        raise ValueError('power routes require the reviewed two-row clock layout')
    supplies = ('VDD', 'VSS', 'VLOGIC')
    buses = folded['row_buses']
    if any(net not in buses for net in supplies):
        raise ValueError('clock supply row is missing')
    columns = []

    def box(layer, x0, y0, x1, y1):
        top.shapes(layout.layer(layer, 0)).insert(db.Box(*(round(v / layout.dbu)
            for v in (x0, y0, x1, y1))))

    for part in info['components']:
        if part['kind'] != 'mos' or part['connections']['S'] not in supplies:
            continue
        net = part['connections']['S']
        dx, dy = part['origin_um']
        sx, sy = part['geometry']['terminals_um']['S']
        sx, sy = sx + dx, sy + dy
        # Each component sits above exactly one corresponding row supply bus.
        below = [b for b in buses[net] if b['y_um'] < sy]
        if not below:
            raise ValueError('source has no supply bus below its row')
        bus = max(below, key=lambda b: b['y_um'])
        by = bus['y_um']
        if sy - by >= folded['pitch_um']:
            raise ValueError('source supply branch crosses another row')
        x = sx - 1
        left = min(bus['x_min_um'], x - 1.67)
        right = max(bus['x_max_um'], x + 1.67)
        box(67, left - .2, by - 2.5, right + .2, by + 2.5)
        bus.update(x_min_um=left, x_max_um=right)
        # Keep this column inside the MOS gutter/body extent, beyond the four
        # original signal columns. One horizontal row of eight cuts fits even
        # the 1 um tuning transistor without touching its drain or gate.
        box(30, x - 1.67, sy - .2, max(sx + .2, x + 1.67), sy + .2)
        box(50, x - 1.67, by - .2, x + 1.67, sy + .2)
        for layer, y in ((49, sy), (66, by)):
            for index in range(8):
                cx = x + (index - 3.5) * .42
                box(layer, cx - .095, y - .095, cx + .095, y + .095)
        columns.append(dict(component=part['name'], net=net, row=bus['row'],
            x_um=x, source_um=[sx, sy], bus_y_um=by, width_um=3.34, cuts_per_landing=8))
    if not columns:
        raise ValueError('clock has no supply-fed custom transistors')

    rails = []
    for net in supplies:
        for bus in buses[net]:
            if bus['width_um'] != 5:
                raise ValueError('clock upper rail requires the full-width supply bus')
            left, right, y = bus['x_min_um'], bus['x_max_um'], bus['y_um']
            if right - left < 4:
                raise ValueError('clock supply bus is too short for the upper rail')
            # Parallel the existing Metal5 with TopMetal1. A regular grid
            # avoids overlapping partial via arrays at closely spaced taps.
            # The 3.8 um rail also preserves TopMetal1 spacing between supplies.
            box(126, left - .2, y - 1.9, right + .2, y + 1.9)
            first = math.ceil((left + .65) / .84) * .84
            count = 0
            for index in range(math.floor((right - .65 - first) / .84) + 1):
                x = first + index * .84
                for row in range(4):
                    cy = y + (row - 1.5) * .84
                    box(125, x - .21, cy - .21, x + .21, cy + .21)
                    count += 1
            if count < 4:
                raise ValueError('clock upper rail has no via array')
            rails.append(dict(net=net, row=bus['row'], y_um=y, width_um=3.8,
                              x_min_um=left, x_max_um=right, cuts=count))
    spines = []
    power_spines = [(net, folded['spines'][net]) for net in supplies if net in folded['spines']]
    xs = sorted(spine['x_um'] for _, spine in power_spines)
    if any(b - a < 5.8 - 1e-9 for a, b in zip(xs, xs[1:])):
        raise ValueError('upper supply spines lack TopMetal2 spacing')
    for net, spine in power_spines:
        x = spine['x_um']
        levels = [bus['y_um'] for bus in buses[net]]
        box(134, x - 1.9, min(levels) - 2, x + 1.9, max(levels) + 2)
        for y in levels:
            # Two vertically arranged 0.9 um TopVia2 cuts, with the pinned
            # 1.06 um cut spacing and 0.5 um conductor enclosures.
            box(126, x - 1.05, y - 2, x + 1.05, y + 2)
            for delta in (-.98, .98):
                box(133, x - .45, y + delta - .45, x + .45, y + delta + .45)
        spines.append(dict(net=net, layer=134, x_um=x, width_um=3.8,
                           row_levels_um=levels, cuts_per_row=2))
    info['direct_clock_supply_columns'] = columns
    info['upper_clock_supply_rails'] = rails
    info['upper_clock_supply_spines'] = spines
    info.update(width_um=top.bbox().width() * layout.dbu,
                height_um=top.bbox().height() * layout.dbu,
                bounding_area_um2=top.bbox().area() * layout.dbu ** 2)
