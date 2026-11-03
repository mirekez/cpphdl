"""Parallel upper-metal returns for the physical PLL supply rails."""
import math

import klayout.db as db


def add_ground_straps(layout, top, info):
    folded = info['folded_rows']
    rails = [rail for rail in info['upper_clock_supply_rails'] if rail['net'] == 'VSS']
    spine = folded['spines']['VSS']
    if len(rails) != 2 or not math.isclose(layout.dbu, .001):
        raise ValueError('ground straps require the reviewed two-row PLL')
    other_spines = [s['x_um'] for n, s in folded['spines'].items() if n != 'VSS']
    left = spine['x_um'] - 1.9
    if any(x + 1.9 > left - 2 + 1e-9 for x in other_spines):
        raise ValueError('ground strap would approach another supply spine')
    ground_column = info['direct_control_storage_routes']['ground_column_x_um']
    records = []

    def box(layer, x0, y0, x1, y1):
        top.shapes(layout.layer(layer, 0)).insert(db.Box(*(round(v / layout.dbu)
            for v in (x0, y0, x1, y1))))

    for rail in rails:
        y, right = rail['y_um'], rail['x_max_um']
        # Metal5/TopMetal1 carry the closely spaced row signals and supplies.
        # TopMetal2 crosses them without contact. Only the ground rail gets
        # these cuts; the existing vertical control-capacitor return is ground.
        box(134, left, y - 6, right + .2, y + 6)
        cuts = 0
        for index in range(math.floor((right - spine['x_um'] - 1) / 7.84) + 1):
            x = spine['x_um'] + index * 7.84
            if abs(x - ground_column) < 1.96:
                continue
            box(126, x - 1.05, y - 2, x + 1.05, y + 2)
            for delta in (-.98, .98):
                box(133, x - .45, y + delta - .45, x + .45, y + delta + .45)
                cuts += 1
        records.append(dict(row=rail['row'], y_um=y, x_min_um=left,
                            x_max_um=right + .2, width_um=12, via_cuts=cuts))
    info['upper_ground_straps'] = records
    info.update(width_um=top.bbox().width() * layout.dbu,
                height_um=top.bbox().height() * layout.dbu,
                bounding_area_um2=top.bbox().area() * layout.dbu ** 2)
