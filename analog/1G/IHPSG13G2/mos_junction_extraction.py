"""Restore HV MOS junction areas from GDS diffusion and extracted terminal positions.

Opt-in binding for the verified straight-finger MOS macros. Magic can lose
junction area when D and S share a conductor; AS=0 additionally requests
PDK default geometry. Each actual diffusion polygon is partitioned equally
among its adjacent fingers, retaining all distributed terminal nodes and RC.
The caller must first verify independent LVS and extraction provenance.
"""
import math
from pathlib import Path
import re
import shlex

from extract_analog import digest, number

MODELS = {'sg13_hv_nmos', 'sg13_hv_pmos'}


def restore(gds, geometry, coordinates, text):
    import klayout.db as db
    gds, coordinates = Path(gds).resolve(), Path(coordinates).resolve()
    layout = db.Layout()
    layout.read(str(gds))
    top = layout.top_cell()
    if top is None or not math.isclose(layout.dbu, .001):
        raise ValueError('unreviewed MOS geometry database')
    ext = coordinates.read_text()
    scales = re.findall(r'^scale \S+ \S+ (\S+)$', ext, re.M)
    if len(scales) != 1 or not math.isfinite(float(scales[0])) or float(scales[0]) <= 0:
        raise ValueError('unreviewed extraction coordinate scale')
    scale = float(scales[0]) * .01 / layout.dbu
    def point(x, y):
        a, b = float(x) * scale, float(y) * scale
        if abs(a - round(a)) > 1e-8 or abs(b - round(b)) > 1e-8:
            raise ValueError('extraction coordinates off the GDS grid')
        return db.Point(round(a), round(b))
    matches = re.findall(r'^rnode "([^"]+)" \S+ \S+ (-?\d+) (-?\d+) \S+$', ext, re.M)
    positions = {name: point(x, y) for name, x, y in matches}
    if len(positions) != len(matches):
        raise ValueError('duplicate extracted terminal position')
    devices = {}
    for line in ext.splitlines():
        f = shlex.split(line)
        if f[:2] != ['device', 'msubckt'] or f[2] not in MODELS:
            continue
        if len(f) != 17:
            raise ValueError('changed MOS extraction record')
        key = (f[14], f[8], f[11], f[7], f[2])  # D, G, S, B, model
        if key in devices:
            raise ValueError('ambiguous extracted MOS terminals')
        devices[key] = dict(anchor=point(f[3], f[4]))
    if not devices:
        raise ValueError('missing extracted HV MOS devices')
    anchors, components = {}, {}
    for part in geometry['components']:
        if part['kind'] != 'mos':
            continue
        g, name = part['geometry'], part['name']
        if g['model'] not in MODELS or name in components:
            raise ValueError('unreviewed MOS macro')
        instances = [i for i in top.each_inst() if i.cell.name == name]
        if len(instances) != 1:
            raise ValueError('ambiguous MOS macro instance')
        instance = instances[0]
        origin = db.Point(*(round(v / layout.dbu) for v in part['origin_um']))
        if instance.trans != db.Trans(origin):
            raise ValueError('MOS placement differs from geometry proof')
        cell = instance.cell
        active = db.Region(cell.begin_shapes_rec(layout.layer(1, 0))).merged()
        poly = db.Region(cell.begin_shapes_rec(layout.layer(5, 0))).merged()
        height = round(g['finger_width_um'] / layout.dbu)
        def stripes(region):
            selected = [p for p in region.each() if p.bbox().bottom == 0 and p.bbox().top == height]
            if any(p.area() != p.bbox().area() for p in selected):
                raise ValueError('nonrectangular native MOS stripe')
            return sorted(selected, key=lambda p: p.bbox().left)
        gates = stripes((active & poly).merged())
        diffusion = stripes((active - poly).merged())
        if (len(gates) != g['fingers'] or len(diffusion) != len(gates) + 1 or
                any(p.bbox().width() != round(g['length_um'] / layout.dbu) for p in gates)):
            raise ValueError('changed GDS channel/diffusion inventory')
        record = components[name] = dict(diffusion=diffusion, used=[0] * len(diffusion), origin=origin,
            expected_sharing=[1] + [2] * (len(diffusion) - 2) + [1], geometry=g,
            before_area_m2=0., before_perimeter_m=0., after_area_m2=0., after_perimeter_m=0.)
        for i, gate in enumerate(gates):
            if diffusion[i].bbox().right != gate.bbox().left or diffusion[i + 1].bbox().left != gate.bbox().right:
                raise ValueError('diffusion is not adjacent to its channel')
            anchor = gate.bbox().p1 + origin
            key = (anchor.x, anchor.y)
            if key in anchors:
                raise ValueError('overlapping MOS channel anchors')
            anchors[key] = (name, i)
    if len(anchors) != len(devices):
        raise ValueError('GDS and extracted MOS counts differ')
    lines, changes, used = [], [], set()
    for line in text.splitlines():
        f = line.split()
        if not f or not f[0].startswith('X') or not any(model in f for model in MODELS):
            lines.append(line)
            continue
        if len(f) != 12 or f[5] not in MODELS:
            raise ValueError('unreviewed extracted MOS parameters')
        parameters = dict(item.split('=') for item in f[6:])
        if set(parameters) != {'w', 'l', 'as', 'ad', 'ps', 'pd'}:
            raise ValueError('explicit junction parameters required')
        old = {k: number(v, allow_zero=k not in ('w', 'l')) for k, v in parameters.items()}
        key = tuple(f[1:6])
        if key not in devices or key in used:
            raise ValueError('netlist MOS does not uniquely match extraction coordinates')
        used.add(key)
        anchor = devices[key]['anchor']
        anchor_key = (anchor.x, anchor.y)
        if anchor_key not in anchors:
            raise ValueError('MOS channel missing from GDS')
        name, index = anchors.pop(anchor_key)
        component = components[name]
        g = component['geometry']
        if (f[5] != g['model'] or
                not math.isclose(old['w'], g['finger_width_um'] * 1e-6, rel_tol=1e-10) or
                not math.isclose(old['l'], g['length_um'] * 1e-6, rel_tol=1e-10)):
            raise ValueError('MOS dimensions differ from GDS')
        values, chosen = {}, []
        for terminal, node in (('d', f[1]), ('s', f[3])):
            if node not in positions:
                raise ValueError('missing MOS terminal position')
            local = positions[node] - component['origin']
            adjacent = [i for i in (index, index + 1) if component['diffusion'][i].inside(local)]
            if len(adjacent) != 1:
                raise ValueError('MOS terminal is not inside an adjacent diffusion')
            stripe = adjacent[0]
            chosen.append(stripe)
            component['used'][stripe] += 1
            p = component['diffusion'][stripe]
            sharing = component['expected_sharing'][stripe]
            values['a' + terminal] = p.area() * (layout.dbu * 1e-6) ** 2 / sharing
            values['p' + terminal] = p.perimeter() * layout.dbu * 1e-6 / sharing
        if len(set(chosen)) != 2:
            raise ValueError('both MOS terminals assigned to one diffusion')
        changed = any(not math.isclose(old[k], v, rel_tol=1e-10, abs_tol=1e-30) for k, v in values.items())
        if changed:
            f[6:] = [k + '=' + (f'{values[k]:.17g}' if k in values else v) for k, v in parameters.items()]
            line = ' '.join(f)
            changes.append(dict(device=f[0], component=name, original={k: old[k] for k in values}, corrected=values))
        for field, keys in (('area_m2', ('as', 'ad')), ('perimeter_m', ('ps', 'pd'))):
            component['before_' + field] += sum(old[k] for k in keys)
            component['after_' + field] += sum(values[k] for k in keys)
        lines.append(line)
    if len(used) != len(devices) or anchors:
        raise ValueError('incomplete extracted MOS geometry binding')
    totals = {}
    for name, c in components.items():
        if c['used'] != c['expected_sharing']:
            raise ValueError('diffusion area was omitted or counted twice')
        area = sum(p.area() for p in c['diffusion']) * (layout.dbu * 1e-6) ** 2
        perimeter = sum(p.perimeter() for p in c['diffusion']) * layout.dbu * 1e-6
        if not math.isclose(area, c['after_area_m2'], rel_tol=1e-12) or not math.isclose(perimeter, c['after_perimeter_m'], rel_tol=1e-12):
            raise ValueError('junction geometry not conserved')
        totals[name] = {k: c[k] for k in ('before_area_m2', 'before_perimeter_m', 'after_area_m2', 'after_perimeter_m')}
        totals[name].update(gds_area_m2=area, gds_perimeter_m=perimeter)
    return '\n'.join(lines) + '\n', dict(devices=len(devices), changes=changes, components=totals,
        source_sha256={str(p): digest(p) for p in (Path(__file__).resolve(), gds, coordinates)}, scope=__doc__)
