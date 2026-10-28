"""Give the receive-reference sink distributed access to its current collectors.

Eight Via3/Via4 cuts per stripe reduce local crowding; two TopVia1 cuts
connect each Metal5 post to the wide collector. This optional candidate
requires electrical, current-sharing and full-chip reliability checks.
"""
import math
import klayout.db as db


def add_receive_upper_current_routes(layout, top, info):
    if (not math.isclose(layout.dbu, .001) or info.get('via_columns') != 1 or
            'receive_current_routes' not in info or 'receive_drain_contacts' not in info or
            'kelvin_routes' not in info or 'sink_upper_current' in info):
        raise ValueError('upper current routing requires the prepared sense overlay once')
    sink, = [p for p in info['components'] if p['name'] == 'ReceiveCommonMOS10']
    g = sink['geometry']
    if (g['model'] != 'sg13_hv_nmos' or g['fingers'] != 30 or g['finger_width_um'] != 5 or g['length_um'] != .45 or
            sink['connections'] != dict(D='OUT', G='DRIVE', S='VSS', B='VSS')):
        raise ValueError('changed sink geometry or purpose')
    cell = layout.cell(sink['name'])
    native, = list(cell.each_inst())
    stripes = sorted({s.bbox().to_s(): s.bbox() for s in native.cell.each_shape(layout.layer(8, 0))
        if s.is_box() and s.bbox().height() == round(5 / layout.dbu)}.values(), key=lambda b: b.left)
    if (len(stripes) != 31 or native.trans != db.Trans() or
            g['terminals_um']['D'][1] != 4.55 or g['terminals_um']['S'][1] != .45):
        raise ValueError('sink stripe inventory or placement changed')
    for net, layer in (('OUT', 126), ('VSS', 134)):
        pin = info['terminals'][net]
        if pin['layer'] != layer or not math.isfinite(pin['y_um']):
            raise ValueError('upper current collector pin changed')
    if not math.isfinite(info['receive_current_routes']['output_drain_column_x_um']):
        raise ValueError('upper current drain column changed')
    dx, dy = sink['origin_um']

    def rectangle(x0, y0, x1, y1):
        return db.Box(*(round(v / layout.dbu) for v in (x0, y0, x1, y1)))

    def box(layer, x0, y0, x1, y1):
        top.shapes(layout.layer(layer, 0)).insert(rectangle(x0, y0, x1, y1))

    # The old central source cuts conflict with distributed per-stripe cuts.
    # Retain its conductors and bottom cuts; new source posts reconnect it.
    column, = [c for c in info['direct_supply_columns'] if c['component'] == sink['name']]
    cx, sy = column['x_um'], column['source_y_um']
    area = rectangle(cx - 1.67, sy - .62, cx + 1.67, sy + .62)
    old = [s for s in top.each_shape(layout.layer(49, 0)) if area.contains(s.bbox().p1) and area.contains(s.bbox().p2)]
    if len(old) != 24:
        raise ValueError('original central source Via3 array changed')
    # Validate before removing any original geometry. All later additions use
    # these prevalidated coordinates and the unchanged sink device inventory.
    if not all(math.isfinite(v) for v in (dx, dy, cx, sy)):
        raise ValueError('nonfinite sink placement')
    for cut in old:
        cut.delete()

    collections = {}
    for terminal, selected, base_y, direction, collector_y in (
            ('D', stripes[1::2], dy + 4.55, -1, dy + 4.55),
            ('S', stripes[::2], dy + .45, 1, dy + .45)):
        xs = [dx + s.center().x * layout.dbu for s in selected]
        ys = [base_y + direction * .42 * i for i in range(4)]
        for x in xs:
            # Eight actual Via3/Via4 cuts per stripe: 3.2 mA sum capacity
            # at the pinned PDK's 0.4 mA/cut limit. Sharing still needs checking.
            for layer in (30, 50):
                box(layer, x - .41, min(ys) - .2, x + .41, max(ys) + .2)
            # Metal5 collects the stripe currents before reaching TopMetal1.
            # Its 0.8 um width supports 1.6 mA at 2 mA/um.
            box(67, x - .4, min(ys) - .2, x + .4, max(ys) + .2)
            for cut in (49, 66):
                for y in ys:
                    for shift in (-.21, .21):
                        box(cut, x + shift - .095, y - .095, x + shift + .095, y + .095)
            box(67, x - .4, collector_y - .75, x + .4, collector_y + .75)
            for delta in (-.42, .42):
                box(125, x - .21, collector_y + delta - .21, x + .21, collector_y + delta + .21)
        box(126, min(xs) - 1.2, collector_y - 1.2, max(xs) + 1.2, collector_y + 1.2)
        collections[terminal] = dict(stripes=len(xs), via3_cuts_per_stripe=8,
            via4_cuts_per_stripe=8, topvia1_cuts_per_stripe=2, collector_width_um=2.4, metal5_post_width_um=.8)
        if terminal == 'D':
            right = info['receive_current_routes']['output_drain_column_x_um'] + .8
            by = info['terminals']['OUT']['y_um']
            box(126, min(xs) - 1.2, collector_y - 1.2, right + 1.2, collector_y + 1.2)
            box(126, right - 1.2, by - 1.2, right + 1.2, collector_y + 1.2)
        else:
            box(134, min(xs) - 1.05, collector_y - 1.05, max(xs) + 1.05, collector_y + 1.05)
            for x in xs[::2]:
                box(133, x - .45, collector_y - .45, x + .45, collector_y + .45)
            by = info['terminals']['VSS']['y_um']
            box(134, cx - 1.05, by - 1.05, cx + 1.05, collector_y + 1.05)
            collections[terminal]['topvia2_cuts'] = len(xs[::2])
    info['sink_upper_current'] = dict(collectors=collections, removed_central_source_via3=24,
        scope='distributed real conductors/cuts; local DRC/LVS, electrical and current-sharing qualification required')
    info.update(width_um=top.bbox().width() * layout.dbu, height_um=top.bbox().height() * layout.dbu,
        bounding_area_um2=top.bbox().area() * layout.dbu ** 2)
