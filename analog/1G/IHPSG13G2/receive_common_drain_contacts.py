"""Distributed Via2 access to the receive-reference output sink's drain."""
import klayout.db as db


def add_receive_drain_contacts(layout, top, info):
    if 'receive_current_routes' not in info or 'receive_drain_contacts' in info:
        raise ValueError('drain access requires the current-route overlay once')
    sink, = [p for p in info['components'] if p['kind'] == 'mos'
             and p['connections'].get('D') == 'OUT']
    geometry = sink['geometry']
    if (geometry['model'] != 'sg13_hv_nmos' or geometry['fingers'] != 30 or
            geometry['finger_width_um'] != 5 or geometry['length_um'] != .45 or
            geometry.get('output_drain_via1_per_stripe') != 9 or
            geometry.get('supply_source_via2_per_stripe') != 9):
        raise ValueError('unreviewed receive sink contact geometry')
    cell = layout.cell(sink['name'])
    instance, = list(cell.each_inst())
    stripes = {s.bbox().to_s(): s.bbox()
               for s in instance.cell.each_shape(layout.layer(8, 0))
               if s.is_box() and s.bbox().height() == round(5 / layout.dbu)}
    stripes = sorted(stripes.values(), key=lambda b: b.left)
    if len(stripes) != 31 or geometry['terminals_um']['D'][1] != 4.55:
        raise ValueError('receive sink diffusion inventory changed')
    dx, dy = sink['origin_um']

    def box(layer, x0, y0, x1, y1):
        top.shapes(layout.layer(layer, 0)).insert(db.Box(*(round(v / layout.dbu)
            for v in (x0 + dx, y0 + dy, x1 + dx, y1 + dy))))

    # The source collector ends at y=1.07 um and the source stripes at
    # y=4.01 um. Keep drain columns above 1.41 um and its collector above
    # 4.35 um. This retains separation despite interleaved source fingers.
    ys = [4.55 - .42 * index for index in range(8)]
    xs = [stripe.center().x * layout.dbu for stripe in stripes[1::2]]
    for x in xs:
        box(30, x - .2, min(ys) - .2, x + .2, max(ys) + .2)
        for y in ys:
            box(29, x - .095, y - .095, x + .095, y + .095)
    box(30, min(xs) - .2, 4.35, max(xs) + .2, 4.75)
    info['receive_drain_contacts'] = dict(component=sink['name'], stripes=len(xs),
        via2_cuts_per_stripe=len(ys), retained_width_um=150, retained_length_um=.45)
