"""Sense the receive reference at its external pins, away from load-current drops.

This candidate retains the IC's devices and storage. Full extracted transient
qualification is required before selecting it as the default implementation.
"""
import math

import klayout.db as db


def add_receive_sense_routes(layout, top, info):
    if (not math.isclose(layout.dbu, .001) or info.get('via_columns') != 1 or
            'receive_drain_contacts' not in info or 'kelvin_routes' in info):
        raise ValueError('sense routing requires the original current-route/drain overlay once')
    components = {part['name']: part for part in info['components']}
    expected = {
        'ReceiveCommonResistance0': dict(PLUS='VDD', MINUS='TARGET', SUB='VSS'),
        'ReceiveCommonResistance1': dict(PLUS='TARGET', MINUS='VSS', SUB='VSS'),
        'ReceiveCommonMOS1': dict(D='LEFT', G='OUT', S='TAIL', B='VSS'),
    }
    for name, connections in expected.items():
        if name not in components or components[name]['connections'] != connections:
            raise ValueError('changed receive sense topology')

    def rectangle(x0, y0, x1, y1):
        if x0 >= x1 or y0 >= y1:
            raise ValueError('degenerate sense route')
        return db.Box(*(round(v / layout.dbu) for v in (x0, y0, x1, y1)))

    def box(layer, x0, y0, x1, y1):
        top.shapes(layout.layer(layer, 0)).insert(rectangle(x0, y0, x1, y1))

    def stack(x, y, upper):
        # The native Metal4 escape remains. These cuts carry divider or gate
        # current, while the existing current collectors carry the sink load.
        for layer in (50, 67):
            box(layer, x - .2, y - .2, x + .2, y + .2)
        box(66, x - .095, y - .095, x + .095, y + .095)
        box(67, x - .4, y - .4, x + .4, y + .4)
        box(125, x - .21, y - .21, x + .21, y + .21)
        box(126, x - 1.05, y - 1.05, x + 1.05, y + 1.05)
        if upper:
            box(133, x - .45, y - .45, x + .45, y + .45)
            box(134, x - 1.05, y - 1.05, x + 1.05, y + 1.05)

    top_y = top.bbox().top * layout.dbu
    plans = [('ReceiveCommonResistance0', 'PLUS', 'VDD', 126, top_y + 5),
             ('ReceiveCommonResistance1', 'MINUS', 'VSS', 134, top_y + 10),
             ('ReceiveCommonMOS1', 'G', 'OUT', 134, None)]
    prepared = []
    for component, terminal, net, layer, high in plans:
        matches = [e for e in info['escapes']
                   if e['component'] == component and e['terminal'] == terminal]
        if len(matches) != 1 or matches[0]['net'] != net:
            raise ValueError('sense escape binding differs')
        x, y = matches[0]['location_um']
        pin = info['terminals'][net]
        if pin['layer'] != (126 if net == 'OUT' else layer):
            raise ValueError('unexpected sense source pin layer')
        px, py = pin['x_um'], pin['y_um']
        old = rectangle(x - .2, py - .2, x + .2, y + .2)
        victims = [s for s in top.each_shape(layout.layer(50, 0)) if s.bbox() == old]
        if len(victims) != 1:
            raise ValueError('ambiguous loaded sense branch: ' + net)
        prepared.append((component, terminal, net, layer, high, x, y, px, py, victims[0]))

    evidence = []
    for component, terminal, net, layer, high, x, y, px, py, victim in prepared:
        # A parallel wire would share the load drop. Remove just the original
        # sense branch before reconnecting it directly to the external pin.
        victim.delete()
        stack(x, y, layer == 134)
        if net == 'OUT':
            box(126, px - 1.05, py - 1.05, px + 1.05, py + 1.05)
            box(133, px - .45, py - .45, px + .45, py + .45)
            box(134, min(px, x) - 1.05, py - 1.05, max(px, x) + 1.05, py + 1.05)
            box(134, x - 1.05, min(y, py) - 1.05, x + 1.05, max(y, py) + 1.05)
        else:
            box(layer, px - 1.05, py - 1.05, px + 1.05, high + 1.05)
            box(layer, min(px, x) - 1.05, high - 1.05, max(px, x) + 1.05, high + 1.05)
            box(layer, x - 1.05, y - 1.05, x + 1.05, high + 1.05)
        evidence.append(dict(net=net, component=component, terminal=terminal,
            sense_escape_um=[x, y], external_pin_um=[px, py], layer=layer,
            removed_loaded_metal4_branch=True))
    info['kelvin_routes'] = dict(branches=evidence,
        scope='separate sense connections; original devices, storage and current paths retained; electrical qualification required')
    info.update(width_um=top.bbox().width() * layout.dbu,
        height_um=top.bbox().height() * layout.dbu,
        bounding_area_um2=top.bbox().area() * layout.dbu ** 2)
