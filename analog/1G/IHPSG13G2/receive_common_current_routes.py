"""Parallel physical conductors for the shared receive-reference load current.

This overlay retains every transistor, resistor, storage element and original
wire. It needs fresh DRC/LVS and extracted electrical qualification before use.
"""
import math

import klayout.db as db


def add_receive_current_routes(layout, top, info):
    if (not math.isclose(layout.dbu, .001) or
            info.get('export_inventory') != dict(mos=11, resistors=9,
                capacitor_contributions=19, merged_storage_branches=18) or
            'receive_current_routes' in info):
        raise ValueError('unreviewed receive-reference current topology')
    sink, = [part for part in info['components'] if part['kind'] == 'mos'
             and part['connections'].get('D') == 'OUT']
    if (sink['connections'] != dict(D='OUT', G='DRIVE', S='VSS', B='VSS') or
            sink['geometry']['total_width_um'] != 150 or
            sink['geometry']['length_um'] != .45):
        raise ValueError('receive-reference sink binding changed')
    ports = info['terminals']
    if any(ports[net]['layer'] != 67 for net in ('OUT', 'VDD', 'VSS')):
        raise ValueError('unexpected original receive-reference pin layers')

    def box(layer, x0, y0, x1, y1):
        if x0 >= x1 or y0 >= y1:
            raise ValueError('invalid current-route rectangle')
        top.shapes(layout.layer(layer, 0)).insert(db.Box(*(round(v / layout.dbu)
            for v in (x0, y0, x1, y1))))

    # The drain already has distributed diffusion contacts. Reach its far
    # end through the right gutter, independently of the central source
    # column. The original left escape remains in parallel.
    dx, dy = sink['origin_um']
    px, py = sink['geometry']['terminals_um']['D']
    x, y = dx + px, dy + py
    escape = dx + layout.cell(sink['name']).bbox().right * layout.dbu + 1
    by = ports['OUT']['y_um']
    box(30, x - .2, y - .2, escape + .83, y + .2)
    box(30, escape - .83, y - .62, escape + .83, y + .62)
    box(50, escape - .83, by - .62, escape + .83, y + .62)
    box(67, escape - .83, by - .62, escape + .83, by + .62)
    for layer, cy in ((49, y), (66, by)):
        for ox in (-.63, -.21, .21, .63):
            for oy in (-.42, 0, .42):
                box(layer, escape + ox - .095, cy + oy - .095,
                    escape + ox + .095, cy + oy + .095)

    # The first signal bus is OUT. TopMetal1 can parallel it below the
    # component row without crossing any MIM top plate.
    xs = [e['location_um'][0] for e in info['escapes'] if e['net'] == 'OUT']
    left, right = min(xs) - .2, max(*xs, escape) + .83
    box(67, left, by - 1, right, by + 1)
    box(126, left, by - .82, right, by + .82)
    first = math.ceil((left + .65) / .84) * .84
    for index in range(math.floor((right - .65 - first) / .84) + 1):
        cx = first + index * .84
        box(125, cx - .21, by - .21, cx + .21, by + .21)

    # Supply rails are only 5.76 um apart. Keep VDD on TopMetal1 and use
    # TopMetal2 for the wider ground return, without a cut at VDD crossings.
    ground_xs = [e['location_um'][0] for e in info['escapes'] if e['net'] == 'VSS']
    ground_xs += [c['x_um'] for c in info['direct_supply_columns'] if c['net'] == 'VSS']
    gx0, gx1 = min(ground_xs) - .2, max(ground_xs) + .2
    gy = ports['VSS']['y_um']
    box(134, gx0, gy - 6, gx1, gy + 6)
    first = math.ceil((gx0 + 1.1) / 7.84) * 7.84
    cuts = 0
    for index in range(math.floor((gx1 - 1.1 - first) / 7.84) + 1):
        cx = first + index * 7.84
        box(126, cx - 1.05, gy - 2, cx + 1.05, gy + 2)
        for delta in (-.98, .98):
            box(133, cx - .45, gy + delta - .45, cx + .45, gy + delta + .45)
            cuts += 1

    # Integration must contact the upper conductors directly. Moving pin
    # markers does not remove lower metal or short disconnected layers.
    changes = []
    for net, target in (('OUT', 126), ('VDD', 126), ('VSS', 134)):
        pin = ports[net]
        point = db.Point(round(pin['x_um'] / layout.dbu), round(pin['y_um'] / layout.dbu))
        labels = [s for s in top.shapes(layout.layer(67, 25)).each()
                  if s.is_text() and s.text.string == net]
        markers = [s for s in top.shapes(layout.layer(67, 2)).each()
                   if (s.is_box() or (s.is_polygon() and s.polygon.is_box()))
                   and s.bbox().contains(point)]
        if len(labels) != 1 or len(markers) != 1:
            raise ValueError('ambiguous receive-reference pin: ' + net)
        landing = markers[0].bbox()
        conductor = db.Region(top.begin_shapes_rec(layout.layer(target, 0)))
        if not (db.Region(landing) - conductor).is_empty():
            raise ValueError('receive-reference pin lacks upper conductor: ' + net)
        top.shapes(layout.layer(target, 2)).insert(landing)
        top.shapes(layout.layer(target, 25)).insert(labels[0].text)
        labels[0].delete()
        markers[0].delete()
        pin['layer'] = target
        changes.append(dict(net=net, from_layer=67, to_layer=target))
    info['receive_current_routes'] = dict(output_drain_column_x_um=escape,
        output_bus_width_um=1.64, ground_width_um=12, ground_via_cuts=cuts,
        promoted_ports=changes, scope='added conductors only; electrical qualification required')
    info.update(width_um=top.bbox().width() * layout.dbu,
                height_um=top.bbox().height() * layout.dbu,
                bounding_area_um2=top.bbox().area() * layout.dbu ** 2)
