"""Direct physical connections between the shared replica and its storage."""
import math

import klayout.db as db


def add_storage_routes(layout, top, info):
    storage, = [part for part in info['components'] if part['kind'] == 'capacitor']
    if storage['connections'] != dict(PLUS='BIAS', MINUS='VSS'):
        raise ValueError('unexpected shared replica storage connection')
    terminals = storage['geometry']['terminals']
    if terminals['PLUS']['layer'] != 126 or terminals['MINUS']['layer'] != 67:
        raise ValueError('unexpected storage plate routing layers')
    dx, dy = storage['origin_um']
    x, y = dx + terminals['PLUS']['x_um'], dy + terminals['PLUS']['y_um']
    if (x, y) != (dx + terminals['MINUS']['x_um'], dy + terminals['MINUS']['y_um']):
        raise ValueError('storage plates must share their access centre')
    by = info['terminals']['BIAS']['y_um']
    gy = info['terminals']['VSS']['y_um']
    points = [escape['location_um'][0] for escape in info['escapes'] if escape['net'] == 'BIAS']
    left, right = min(points) - .4, max(points) + .4
    # The original row access goes around the complete bank's top/right
    # boundary. A direct upper-metal branch reaches the first real plate.
    def box(layer, x0, y0, x1, y1):
        top.shapes(layout.layer(layer, 0)).insert(db.Box(*(round(v / layout.dbu)
            for v in (x0, y0, x1, y1))))

    box(126, left, by - .82, right, by + .82)
    box(126, x - 1.9, by - .82, x + 1.9, y + 1.9)
    first = math.ceil((left + .65) / .84) * .84
    for index in range(math.floor((right - .65 - first) / .84) + 1):
        cx = first + index * .84
        box(125, cx - .21, by - .21, cx + .21, by + .21)

    # Access the bottom plate outside the top electrode. TopMetal2 crosses
    # the BIAS rail without joining it; both landings contain real via arrays.
    gx = dx - 7
    box(67, gx - 1.9, y - 1.9, x + 1.9, y + 1.9)
    box(134, gx - 1.9, gy - 2, gx + 1.9, y + 2)
    for cy in (gy, y):
        box(67, gx - 1.9, cy - 1.9, gx + 1.9, cy + 1.9)
        box(126, gx - 1.9, cy - 2, gx + 1.9, cy + 2)
        for ix in range(4):
            for iy in range(4):
                vx, vy = gx + (ix - 1.5) * .84, cy + (iy - 1.5) * .84
                box(125, vx - .21, vy - .21, vx + .21, vy + .21)
        for delta in (-.98, .98):
            box(133, gx - .45, cy + delta - .45, gx + .45, cy + delta + .45)
    info['direct_storage_routes'] = dict(bias_plate_um=[x, y], bias_rail_y_um=by,
        ground_column_x_um=gx, ground_rail_y_um=gy,
        scope='real plate connections with TopMetal2 ground crossover; requires fresh DRC/LVS/PEX')
    info.update(width_um=top.bbox().width() * layout.dbu,
                height_um=top.bbox().height() * layout.dbu,
                bounding_area_um2=top.bbox().area() * layout.dbu ** 2)
