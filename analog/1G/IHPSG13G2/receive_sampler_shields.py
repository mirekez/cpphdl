"""Ground the spaces between sampler buses without moving device terminals."""
from collections import defaultdict
import math

import klayout.db as db


def add_sampler_shields(layout, top, geometry):
    if not math.isclose(geometry['signal_width_um'], .26) or geometry['via_columns'] != 2:
        raise ValueError('sampler shielding requires reviewed bus/via clearance')
    # A 0.20 um shield plus 0.21 um clearance to the existing via landings
    # needs 1.02 um between bus centres, including their 0.26 um tracks.
    by_x = defaultdict(set)
    for shape in top.each_shape(layout.layer(66, 0)):
        point = shape.bbox().center()
        if point.y < 0:
            # Each escape has two via cuts displaced +/-0.21 um in x.
            for shift in (-.21, .21):
                by_x[point.x + round(shift / layout.dbu)].add(point.y)
    levels = {}
    for net in ('OUT', 'IN', 'CLOCK_N', 'CLOCK'):
        escapes = [e for e in geometry['escapes'] if e['net'] == net]
        candidates = set.intersection(*(by_x[round(e['location_um'][0] / layout.dbu)] for e in escapes))
        if len(candidates) != 1:
            raise ValueError('ambiguous sampler bus level: ' + net)
        levels[net] = candidates.pop() * layout.dbu
    tracks = sorted(levels.values())
    if any(not math.isclose(b - a, 1.02, abs_tol=1e-9) for a, b in zip(tracks, tracks[1:])):
        raise ValueError('sampler signal track spacing changed')
    bounds = top.bbox()
    left, right = bounds.left * layout.dbu - 4, bounds.right * layout.dbu + 4
    ground = geometry['terminals']['VSS']['y_um']

    def box(layer, x0, y0, x1, y1):
        top.shapes(layout.layer(layer, 0)).insert(db.Box(*(round(v / layout.dbu)
            for v in (x0, y0, x1, y1))))

    shield_y = [(a + b) / 2 for a, b in zip(tracks, tracks[1:])]
    for y in shield_y:
        box(67, left - .2, y - .1, right + .2, y + .1)
    # Both ends return to VSS through separate Metal4 columns outside the
    # original route envelope. Other nets cross these columns without vias.
    box(67, left - .2, ground - .2, right + .2, ground + .2)
    for x in (left, right):
        box(50, x - .41, ground - .2, x + .41, max(shield_y) + .2)
        for y in [ground] + shield_y:
            box(67, x - .41, y - .2, x + .41, y + .2)
            for shift in (-.21, .21):
                box(66, x + shift - .095, y - .095, x + shift + .095, y + .095)
    geometry['grounded_bus_shields'] = dict(layer=67, width_um=.2,
        y_um=shield_y, return_columns_um=[left, right], vss_bus_y_um=ground)
    geometry.update(width_um=top.bbox().width() * layout.dbu,
                    height_um=top.bbox().height() * layout.dbu,
                    bounding_area_um2=top.bbox().area() * layout.dbu ** 2)


def expose_held_plate(layout, top, geometry):
    """Connect the downstream load at the actual hold plate's TopMetal1 access."""
    hold, = [p for p in geometry['components'] if p['name'] == 'SamplerStorage7']
    if hold['connections'] != {'PLUS': 'OUT', 'MINUS': 'VSS'}:
        raise ValueError('hold storage terminals changed')
    pin = hold['geometry']['terminals']['PLUS']
    if pin['layer'] != 126:
        raise ValueError('hold plate requires TopMetal1 access')
    x = hold['origin_um'][0] + pin['x_um']
    y = hold['origin_um'][1] + pin['y_um']
    previous = geometry['terminals']['OUT']
    point = db.Point(round(previous['x_um'] / layout.dbu), round(previous['y_um'] / layout.dbu))
    labels = [s for s in top.each_shape(layout.layer(previous['layer'], 25))
              if s.is_text() and s.text.string == 'OUT']
    pins = [s for s in top.each_shape(layout.layer(previous['layer'], 2)) if s.bbox().contains(point)]
    if len(labels) != 1 or len(pins) != 1:
        raise ValueError('ambiguous old sampler output port')
    landing = db.Box(*(round(v / layout.dbu) for v in (x - .16, y - .16, x + .16, y + .16)))
    conductor = db.Region(top.begin_shapes_rec(layout.layer(126, 0)))
    if not (db.Region(landing) - conductor).is_empty():
        raise ValueError('held output pin lacks a physical conductor')
    labels[0].delete()
    pins[0].delete()
    top.shapes(layout.layer(126, 2)).insert(landing)
    top.shapes(layout.layer(126, 25)).insert(db.Text('OUT', db.Trans(round(x / layout.dbu), round(y / layout.dbu))))
    geometry['held_plate_access'] = dict(previous=previous, component=hold['name'], terminal='PLUS')
    geometry['terminals']['OUT'] = dict(layer=126, x_um=x, y_um=y)
