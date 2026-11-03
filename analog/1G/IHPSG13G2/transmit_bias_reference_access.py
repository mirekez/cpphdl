"""Candidate physical access for the transmitter reference transistor.

Parallel metal and via arrays reduce reference-source/drain IR drop. This
optional layout variant preserves devices and external Metal5 ports. It must
pass its own DRC/LVS/extraction and complete loaded transmitter qualification
before replacing the default macro; local DC agreement is insufficient.
"""
from pathlib import Path

import klayout.db as db

from extract_analog import digest


def add_reference_access(layout, top, info):
    """Add the reviewed ten-finger reference access; reject other geometry."""
    references = [c for c in info['components'] if c['name'] == 'BiasReference']
    if len(references) != 1 or 'upper_rail_candidate' in info:
        raise ValueError('requires one unmodified BiasReference')
    reference = references[0]
    geometry = reference['geometry']
    if (reference['connections'] != dict(D='BIAS', G='BIAS', S='VSS', B='VSS')
            or geometry['model'] != 'sg13_hv_nmos'
            or geometry['total_width_um'] != 100 or geometry['length_um'] != .45
            or geometry['fingers'] != 10 or geometry['finger_width_um'] != 10
            or geometry['terminals_um']['S'] != [.15, .45]
            or geometry['terminals_um']['D'] != [.98, 9.55]
            or layout.dbu != .001 or info['via_columns'] != 4
            or info['signal_width_um'] != 2):
        raise ValueError('unreviewed bias reference geometry')
    for net in ('VSS', 'VDD', 'BIAS'):
        if info['terminals'][net]['layer'] != 67:
            raise ValueError('requires original Metal5 bias ports')
    for terminal in ('D', 'S'):
        if len([e for e in info['escapes'] if e['component'] == 'BiasReference'
                and e['terminal'] == terminal]) != 1:
            raise ValueError('ambiguous reference escape')
    def box(layer, x0, y0, x1, y1):
        top.shapes(layout.layer(layer, 0)).insert(db.Box(*(round(v / layout.dbu)
            for v in (x0, y0, x1, y1))))
    routes = []
    for net in ('VSS', 'VDD', 'BIAS'):
        port = info['terminals'][net]
        xs = sorted(set(e['location_um'][0] for e in info['escapes'] if e['net'] == net))
        y = port['y_um']
        half = 1 if net == 'BIAS' else 1.5
        box(67, min(xs) - .4, y - half, max(xs) + .4, y + half)
        box(126, min(xs) - .84, y - half, max(xs) + .84, y + half)
        cuts = []
        for x in xs:
            if cuts and x - cuts[-1] < .84 - 1e-9:
                continue
            cuts.append(x)
            for dy in ((0,) if net == 'BIAS' else (-.42, .42)):
                box(125, x - .21, y + dy - .21, x + .21, y + dy + .21)
        routes.append(dict(net=net, width_um=2 * half, via_columns_x_um=cuts, y_um=y))
    reference, = [c for c in info['components'] if c['name'] == 'BiasReference']
    dx, dy = reference['origin_um']
    for terminal, net, shift in (('S', 'VSS', .42), ('D', 'BIAS', -.42)):
        px, py = reference['geometry']['terminals_um'][terminal]
        x, y = px + dx, py + dy + shift
        escape, = [e for e in info['escapes']
                   if e['component'] == 'BiasReference' and e['terminal'] == terminal]
        ex = escape['location_um'][0]
        by = info['terminals'][net]['y_um']
        # Physical parallel access bypasses the narrow Metal3/4 escape.
        # Existing device contacts, rails and pins remain in place.
        count = 6 if terminal == 'S' else 5
        # Land on every existing diffusion stripe. Extra Via1 rows retain
        # the original active/poly device geometry and its contacts.
        for finger in range(count):
            fx = x + 1.66 * finger
            original_y = y - shift
            box(8, fx - .16, min(original_y, y + shift) - .16,
                fx + .16, max(original_y, y + shift) + .16)
            for row in (0, shift):
                cy = y + row
                box(8, fx - .16, cy - .16, fx + .16, cy + .16)
                box(19, fx - .095, cy - .095, fx + .095, cy + .095)
            for layer in (10, 30, 50, 67):
                box(layer, fx - .63, y - .63, fx + .63, y + .63)
            for cut in (29, 49, 66):
                for vx in (-.21, .21):
                    for vy in ((0, shift) if cut == 29 else (-.21, .21)):
                        box(cut, fx + vx - .095, y + vy - .095,
                            fx + vx + .095, y + vy + .095)
            box(125, fx - .21, y - .21, fx + .21, y + .21)
        for layer in (10, 30, 50, 67):
            box(layer, x - .63, y - .63, x + 1.66 * (count - 1) + .63, y + .63)
        box(126, ex - 1, y - 1, x + 1.66 * (count - 1) + 1, y + 1)
        box(134, ex - 1, by - 1, ex + 1, y + 1)
        for cy in (by, y):
            box(126, ex - 1, cy - 1, ex + 1, cy + 1)
            box(133, ex - .45, cy - .45, ex + .45, cy + .45)
        routes.append(dict(net=net, terminal=terminal, reference_access_um=[x,y],
                           upper_column_x_um=ex, width_um=2))
    info['upper_rail_candidate'] = dict(routes=routes, source_sha256=digest(Path(__file__).resolve()),
        scope='parallel real TopMetal1 and TopVia1; original devices and ports retained')
    info.update(width_um=top.bbox().width() * layout.dbu,
                height_um=top.bbox().height() * layout.dbu,
                bounding_area_um2=top.bbox().area() * layout.dbu ** 2)
    return info
