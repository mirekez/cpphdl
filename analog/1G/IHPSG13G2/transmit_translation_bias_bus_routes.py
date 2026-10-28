"""Upper-metal access for the replica's simultaneously switched TX bank."""
import math

import klayout.db as db


def add_loaded_storage_routes(layout, top, info):
    storage, = [part for part in info['components'] if part['kind'] == 'capacitor']
    if (storage['connections'] != dict(PLUS='BIAS', MINUS='VSS') or
            not math.isclose(storage['geometry']['requested_capacitance_F'], 50e-12,
                             rel_tol=1e-12) or storage['geometry']['capacitors'] != 10):
        raise ValueError('unreviewed loaded replica storage')
    routes = info['direct_storage_routes']
    x, y = routes['bias_plate_um']
    by, gy, gx = (routes[key] for key in
                 ('bias_rail_y_um', 'ground_rail_y_um', 'ground_column_x_um'))
    bias_x = [escape['location_um'][0] for escape in info['escapes'] if escape['net'] == 'BIAS']
    left, right = min(bias_x) - .4, max(bias_x) + .4
    width = 8.0

    def box(layer, x0, y0, x1, y1):
        top.shapes(layout.layer(layer, 0)).insert(db.Box(*(round(value / layout.dbu)
            for value in (x0, y0, x1, y1))))

    # Both changes are real parallel metal. Existing cuts and connections
    # remain; intrinsic storage and transistor dimensions do not change.
    box(126, left, by - width / 2, right, by + width / 2)
    box(126, x - width / 2, by - width / 2, x + width / 2, y + width / 2)
    box(67, gx - width / 2, y - width / 2, x + width / 2, y + width / 2)
    box(134, gx - width / 2, gy - width / 2, gx + width / 2, y + width / 2)
    for cy in (gy, y):
        box(67, gx - width / 2, cy - width / 2, gx + width / 2, cy + width / 2)
        box(126, gx - width / 2, cy - width / 2, gx + width / 2, cy + width / 2)
    ground_x = info['terminals']['VSS']['x_um']
    box(134, ground_x - 1, gy - width / 2, gx + width / 2, gy + width / 2)

    promoted = []
    for net, target in (('BIAS', 126), ('VSS', 134)):
        port = info['terminals'][net]
        if port['layer'] != 67:
            raise ValueError('replica supply/bias pin was already relocated')
        point = db.Point(round(port['x_um'] / layout.dbu), round(port['y_um'] / layout.dbu))
        pins = [shape for shape in top.shapes(layout.layer(67, 2)).each()
                if (shape.is_box() or shape.is_polygon()) and shape.bbox().contains(point)]
        labels = [shape for shape in top.shapes(layout.layer(67, 25)).each()
                  if shape.is_text() and shape.text.string == net]
        if len(pins) != 1 or len(labels) != 1:
            raise ValueError('ambiguous replica port: ' + net)
        landing = pins[0].bbox()
        conductor = db.Region(top.begin_shapes_rec(layout.layer(target, 0)))
        if not (db.Region(landing) - conductor).is_empty():
            raise ValueError('replica upper port lacks a real conductor: ' + net)
        top.shapes(layout.layer(target, 2)).insert(landing)
        top.shapes(layout.layer(target, 25)).insert(labels[0].text)
        pins[0].delete()
        labels[0].delete()
        port['layer'] = target
        promoted.append(dict(net=net, from_layer=67, to_layer=target))
    info['loaded_storage_routes'] = dict(width_um=width, promoted_ports=promoted,
        retained_capacitance_F=50e-12,
        scope='real metal and upper access pins; requires new DRC/LVS, RC and loaded-bank checks')
    info.update(width_um=top.bbox().width() * layout.dbu,
                height_um=top.bbox().height() * layout.dbu,
                bounding_area_um2=top.bbox().area() * layout.dbu ** 2)
