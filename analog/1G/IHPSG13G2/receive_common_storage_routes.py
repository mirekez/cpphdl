"""Connect real receiver storage plates to upper current rails without changing capacitance."""
import math
import klayout.db as db


def add_receive_storage_routes(layout, top, info):
    if not math.isclose(layout.dbu, .001) or 'sink_upper_current' not in info or 'storage_access' in info:
        raise ValueError('storage access requires the reviewed eight-cut receiver')
    parts = {p['name']: p for p in info['components']}
    selected = [('ReceiveCommonStorage9', 'OUT', 16, 4),
                ('ReceiveCommonStorage17', 'TARGET', 2, 1)]
    # Validate both banks and rails before adding conductors. These dimensions
    # establish clearance for the bottom-plate vias outside the MIM top plate.
    for net, layer in (('OUT', 126), ('VSS', 134)):
        pin = info['terminals'][net]
        if pin['layer'] != layer or not all(math.isfinite(pin[k]) for k in ('x_um', 'y_um')):
            raise ValueError('storage access requires reviewed upper current pins')
    for name, signal, count, rows in selected:
        if name not in parts:
            raise ValueError('missing receiver storage bank')
        part = parts[name]; g = part['geometry']
        if (part['connections'] != dict(PLUS=signal, MINUS='VSS') or
                g['capacitors'] != count or g['rows'] != rows or
                g['unit']['terminals']['PLUS']['layer'] != 126 or
                g['unit']['terminals']['MINUS']['layer'] != 67):
            raise ValueError('changed storage bank topology')
        if (len(part['origin_um']) != 2 or not all(math.isfinite(v) for v in part['origin_um']) or
                g['columns'] != count // rows or not math.isclose(g['pitch_um'], 63.88) or
                any(not math.isclose(g['unit'][key], 57.68) for key in ('width_um', 'length_um'))):
            raise ValueError('changed storage bank placement or plate dimensions')

    def box(layer, x0, y0, x1, y1):
        if x0 >= x1 or y0 >= y1:
            raise ValueError('invalid storage access rectangle')
        top.shapes(layout.layer(layer, 0)).insert(db.Box(*(round(v / layout.dbu) for v in (x0, y0, x1, y1))))

    evidence = []
    for name, signal, count, rows in selected:
        part = parts[name]; g = part['geometry']; ox, oy = part['origin_um']
        plate = g['unit']['terminals']['MINUS']
        x, y = ox + plate['x_um'], oy + plate['y_um']
        ground_x = ox - 6.4
        gy = info['terminals']['VSS']['y_um']
        row_ys = [y + i * g['pitch_um'] for i in range(rows)]
        # Bottom plates remain Metal5. Lift them outside the top-plate envelope,
        # where the vias cannot short through the MIM dielectric to PLUS.
        box(134, ground_x - 1.2, gy - 1.2, ground_x + 1.2, row_ys[-1] + 2)
        for row_y in row_ys:
            box(67, ground_x - 1.2, row_y - 1.7, x + 1.2, row_y + 1.7)
            box(126, ground_x - 1.2, row_y - 2, ground_x + 1.2, row_y + 2)
            for dx in (-.42, .42):
                for dy in (-1.26, -.42, .42, 1.26):
                    box(125, ground_x + dx - .21, row_y + dy - .21,
                        ground_x + dx + .21, row_y + dy + .21)
            for dy in (-.98, .98):
                box(133, ground_x - .45, row_y + dy - .45,
                    ground_x + .45, row_y + dy + .45)
        if signal == 'OUT':
            pin = info['terminals']['OUT']
            # Contact the first column directly on the conductive top plates,
            # bypassing the original long Metal4 escape at the bank's far end.
            box(126, x - 3, pin['y_um'] - 1.8, x + 3, row_ys[-1] + 3)
            box(126, min(pin['x_um'], x) - 1.8, pin['y_um'] - 1.8,
                max(pin['x_um'], x) + 3, pin['y_um'] + 1.8)
        evidence.append(dict(component=name, rows=rows, ground_column_um=ground_x,
            row_y_um=row_ys, topvia1_per_row=8, topvia2_per_row=2,
            direct_top_plate_access=signal == 'OUT'))
    info['storage_access'] = dict(banks=evidence,
        scope='additional physical conductors only; unchanged devices, MIM plates, sense paths and source declarations')
    info.update(width_um=top.bbox().width() * layout.dbu,
        height_um=top.bbox().height() * layout.dbu,
        bounding_area_um2=top.bbox().area() * layout.dbu ** 2)
