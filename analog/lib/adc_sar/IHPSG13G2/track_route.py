"""Reinforce the physical TRACK bus and its Metal4/Metal5 escapes.

The extracted V13 path from the TRACK pin to D0's gate contains about 52 ohms
in shared wire segments before the final 24 ohm gate landing. This overlay
changes metal only; it needs new DRC, LVS, extraction and electrical checks.
"""
import math
import klayout.db as db


def strengthen_track(layout, top, geometry):
    if not math.isclose(layout.dbu, .001) or geometry.get('via_columns') != 2:
        raise ValueError('TRACK overlay requires the reviewed 1 nm / two-column row')
    if 'track_route' in geometry:
        raise ValueError('TRACK overlay already applied')
    port = geometry['terminals']['TRACK']
    escapes = [e for e in geometry['escapes'] if e['net'] == 'TRACK']
    if port['layer'] != 67 or {(e['component'], e['terminal']) for e in escapes} != {
            ('D0', 'G'), ('D4', 'G'), ('D5', 'G'), ('D7', 'G')}:
        raise ValueError('unexpected sampling-clock escape inventory')

    def box(layer, x0, y0, x1, y1):
        top.shapes(layout.layer(layer, 0)).insert(
            db.Box(*(round(v / .001) for v in (x0, y0, x1, y1))))

    y = port['y_um']
    xs = [e['location_um'][0] for e in escapes]
    box(67, min(xs) - .83, y - .5, max(xs) + .83, y + .5)
    for escape in escapes:
        x, endpoint = escape['location_um']
        # D0's adjacent IN escape is already 1.66 um wide and only 1.56 um
        # away. Keep the original narrow TRACK landing there to avoid a short.
        if escape['component'] == 'D0':
            continue
        box(50, x - .83, y - .2, x + .83, endpoint + .2)
        for offset in (-.63, .63):
            xcut = x + offset
            box(66, xcut - .095, y - .095, xcut + .095, y + .095)
    geometry['track_route'] = dict(metal5_width_um=1., metal4_width_um=1.66,
                                   via4_per_reinforced_escape=4,
                                   retained_narrow_escape='D0.G', escapes=len(escapes))
