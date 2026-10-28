"""Parallel source access cuts for the direct clock-supply routing candidate."""
import klayout.db as db


def add_source_contacts(layout, top, info):
    parts = {part['name']: part for part in info['components']}
    records = []
    if not info.get('direct_clock_supply_columns'):
        raise ValueError('source reinforcement requires direct supply columns')
    def box(layer, x0, y0, x1, y1):
        top.shapes(layout.layer(layer, 0)).insert(db.Box(*(round(v / layout.dbu)
            for v in (x0, y0, x1, y1))))
    for column in info['direct_clock_supply_columns']:
        part = parts[column['component']]
        if part['connections']['S'] != part['connections']['B']:
            raise ValueError('source access crosses a differently biased body')
        sx, sy = column['source_um']
        dx, dy = part['origin_um']
        geometry = part['geometry']
        if abs(sx - dx - .15) > 1e-8:
            raise ValueError('review source stripe before extending first access')
        width = geometry['finger_width_um']
        # Extend only left of the first source stripe: extending across the
        # repeated fingers would short intervening drain stripes on Metal1.
        box(8, sx - .16, min(dy, sy - .16), sx + .16, max(dy + width, sy + .16))
        box(8, sx - 1.42, sy - .16, sx + .16, sy + .16)
        box(10, sx - 3.14, sy - .2, sx + .2, sy + .2)
        box(30, sx - 3.14, sy - .2, sx + .2, sy + .2)
        bx, by = geometry['terminals_um']['B']
        bx, by = bx + dx, by + dy
        if 0 < by - sy - .4 < .21:
            # On 2 um fingers the new source bar approaches the same-net
            # body landing within minimum spacing; join that short gap.
            box(10, bx - .2, sy - .2, bx + .2, by + .2)
        for index in range(1, 4):
            x = sx - index * .42
            box(19, x - .095, sy - .095, x + .095, sy + .095)
        # The first existing Via2 cut is retained. New cuts are on the same
        # regular pitch, with no overlapping partial arrays.
        for index in range(1, 8):
            x = sx - index * .42
            box(29, x - .095, sy - .095, x + .095, sy + .095)
        records.append(dict(component=part['name'], source_um=[sx, sy],
                            first_source_via1_cuts=4, source_via2_cuts=8))
    info['parallel_clock_source_contacts'] = records
