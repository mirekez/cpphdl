"""Direct access to the physical PLL's retained tuning capacitor."""
import math

import klayout.db as db


def add_control_storage_routes(layout, top, info):
    control, = info['protected_nets']
    storage, = [part for part in info['components'] if part['kind'] == 'capacitor'
                and part['connections'] == dict(PLUS=control, MINUS='VSS')]
    geometry = storage['geometry']
    if not math.isclose(geometry['requested_capacitance_F'], 3e-12, rel_tol=1e-12):
        raise ValueError('unreviewed PLL tuning storage')
    if geometry['terminals']['PLUS']['layer'] != 126 or geometry['terminals']['MINUS']['layer'] != 67:
        raise ValueError('unexpected PLL tuning plate layers')
    bus, = info['folded_rows']['row_buses'][control]
    ground, = [b for b in info['folded_rows']['row_buses']['VSS'] if b['row'] == bus['row']]
    dx, dy = storage['origin_um']
    x = dx + geometry['terminals']['PLUS']['x_um']
    y = dy + geometry['terminals']['PLUS']['y_um']
    by, gy = bus['y_um'], ground['y_um']
    left, right = bus['x_min_um'] - .2, bus['x_max_um'] + .2

    def box(layer, x0, y0, x1, y1):
        top.shapes(layout.layer(layer, 0)).insert(db.Box(*(round(v / layout.dbu)
            for v in (x0, y0, x1, y1))))

    # The other signal buses remain on Metal5. The tuning wire alone gains
    # a wider landing and upper-metal parallel route, with real via cuts.
    box(67, left, by - .4, right, by + .4)
    box(126, left, by - .82, right, by + .82)
    box(126, x - 1.9, by - .82, x + 1.9, y + 1.9)
    first = math.ceil((left + .65) / .84) * .84
    for index in range(math.floor((right - .65 - first) / .84) + 1):
        cx = first + index * .84
        box(125, cx - .21, by - .21, cx + .21, by + .21)
    gx = dx - 7
    box(67, gx - 1.9, y - 1.9, x + 1.9, y + 1.9)
    box(134, gx - 1.9, gy - 2, gx + 1.9, y + 2)
    for cy in (gy, y):
        box(67, gx - 1.9, cy - 1.9, gx + 1.9, cy + 1.9)
        box(126, gx - 1.9, cy - 2, gx + 1.9, cy + 2)
        # Avoid a second irregular TopVia1 array over the already meshed
        # ground rail. The plate landing has no pre-existing upper cuts.
        if cy == y:
            for ix in range(4):
                for iy in range(4):
                    vx, vy = gx + (ix - 1.5) * .84, cy + (iy - 1.5) * .84
                    box(125, vx - .21, vy - .21, vx + .21, vy + .21)
        for delta in (-.98, .98):
            box(133, gx - .45, cy + delta - .45, gx + .45, cy + delta + .45)
    info['direct_control_storage_routes'] = dict(net=control, storage=storage['name'],
        plate_um=[x, y], control_bus_y_um=by, ground_column_x_um=gx,
        ground_bus_y_um=gy, retained_capacitance_F=geometry['requested_capacitance_F'])
