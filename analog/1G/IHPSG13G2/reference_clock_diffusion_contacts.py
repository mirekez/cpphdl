"""Distribute clock-buffer diffusion current over real contacts and via arrays."""
import math

import klayout.db as db


def add_diffusion_contacts(layout, top, info):
    records = []
    for part in info['components']:
        geometry = part['geometry']
        if part['kind'] != 'mos' or geometry['finger_width_um'] < 4:
            continue
        cell = layout.cell(part['name'])
        instances = list(cell.each_inst())
        if len(instances) != 1:
            raise ValueError('clock diffusion access requires the original guarded PCell')
        device = instances[0].cell
        width = geometry['finger_width_um']
        stripes = {shape.bbox().to_s(): shape.bbox()
                   for shape in device.each_shape(layout.layer(8, 0))
                   if shape.is_box() and shape.bbox().height() == round(width / layout.dbu)}
        stripes = sorted(stripes.values(), key=lambda b: b.left)
        if len(stripes) != geometry['fingers'] + 1:
            raise ValueError('clock diffusion stripe inventory changed')
        dx, dy = part['origin_um']

        def box(layer, x0, y0, x1, y1):
            top.shapes(layout.layer(layer, 0)).insert(db.Box(*(round(v / layout.dbu)
                for v in (x0 + dx, y0 + dy, x1 + dx, y1 + dy))))

        if geometry['terminals_um']['S'][1] != .45 or geometry['terminals_um']['D'][1] != width - .45:
            raise ValueError('clock source/drain landing heights changed')
        count = 1 + math.floor((width / 2 - .5 - .45) / .42)
        if count < 3:
            raise ValueError('insufficient room for distributed clock contacts')
        # In the tight LV pitch, neighboring source/drain landings cannot be
        # wide over the same y range. Reserve separate lower and upper halves.
        for parity, terminal, direction in ((0, 'S', 1), (1, 'D', -1)):
            y = geometry['terminals_um'][terminal][1]
            ys = [y + direction * index * .42 for index in range(count)]
            selected = stripes[parity::2]
            xs = [stripe.center().x * layout.dbu for stripe in selected]
            for x in xs:
                box(8, x - .16, min(ys) - .16, x + .16, max(ys) + .16)
                for layer in (10, 30):
                    box(layer, x - .2, min(ys) - .2, x + .2, max(ys) + .2)
                for cy in ys:
                    for layer in (19, 29):
                        # The existing first cut may overlap identically; the
                        # geometry union is one cut, never a partial array.
                        box(layer, x - .095, cy - .095, x + .095, cy + .095)
            box(30, min(xs) - .2, y - .2, max(xs) + .2, y + .2)
        records.append(dict(component=part['name'], cuts_per_diffusion_stripe=count,
                            retained_width_um=geometry['total_width_um'],
                            retained_length_um=geometry['length_um']))
    if len(records) != 18:
        raise ValueError('clock diffusion reinforcement inventory changed: ' + str(len(records)))
    info['distributed_clock_diffusion_contacts'] = records
