"""Extend connected row-end wells at verified NW.d corner pairs.

Only the NWell drawing layer changes. This produces a physical candidate;
complete DRC, fresh extraction/LVS and electrical qualification still follow.
"""
import argparse
import hashlib
import json
from pathlib import Path
import re
import xml.etree.ElementTree as ET

import klayout.db as db


def digest(path):
    with Path(path).open('rb') as stream:
        return hashlib.file_digest(stream, 'sha256').hexdigest()


def corner_strips(report, top, dbu):
    """Require opposing vertical corners with the same tie-edge coordinate."""
    groups = {}
    items = ET.parse(report).getroot().findall('./items/item')
    if not items:
        raise ValueError('expected NW.d violations')
    for item in items:
        if item.findtext('category') != "'NW.d'" or item.findtext('cell') != top:
            raise ValueError('only top-level NW.d corners can be repaired')
        values = item.findall('./values/value')
        if len(values) != 1:
            raise ValueError('expected one corner edge pair')
        match = re.fullmatch(r'edge-pair: \(([^;]+);([^\)]+)\)/\(([^;]+);([^\)]+)\)', values[0].text or '')
        if match is None:
            raise ValueError('expected an edge pair')
        points = []
        for text in match.groups():
            coordinates = [float(v) / dbu for v in text.split(',')]
            if len(coordinates) != 2 or any(abs(v - round(v)) > 1e-5 for v in coordinates):
                raise ValueError('off-grid corner')
            points.append(tuple(round(v) for v in coordinates))
        (x, a), (x2, b), (c, y), (d, y2) = points
        if x != x2 or y != y2 or a == b or c == d or x not in (c, d):
            raise ValueError('unexpected corner orientation')
        low, high = sorted((a, b))
        if high - low != 70 or min(abs(y - low), abs(y - high)) != 240:
            raise ValueError('expected the 70 nm corner at 240 nm tie enclosure')
        sign = 1 if y < low else -1 if y > high else 0
        if not sign:
            raise ValueError('active edge crosses well corner')
        base = low if sign == 1 else high
        side = 'left' if b < a else 'right'
        groups.setdefault((base, sign), []).append((x, side))
    strips = []
    for (base, sign), corners in sorted(groups.items()):
        corners.sort()
        if len(corners) % 2 or len({x for x, _ in corners}) != len(corners):
            raise ValueError('unpaired or duplicate row-end corners')
        for left, right in zip(corners[::2], corners[1::2]):
            if left[1] != 'left' or right[1] != 'right' or not 1000 <= right[0] - left[0] <= 500000:
                raise ValueError('unexpected opposing row-end corners')
            # Overlap the existing well by 10 nm. Move the exposed edge 80 nm,
            # leaving 320 nm to the reported tie edge against a 310 nm rule.
            bottom, upper = sorted((base - sign * 10, base + sign * 80))
            strips.append(db.Box(left[0] - 10, bottom, right[0] + 10, upper))
    return strips


def preserve_signature(layout, well_layer):
    result = hashlib.sha256()
    for cell in sorted(layout.each_cell(), key=lambda c: c.name):
        result.update(cell.name.encode() + b'\n')
        for layer in sorted(layout.layer_indexes(), key=lambda i: str(layout.get_info(i))):
            if layer == well_layer:
                continue
            result.update(str(layout.get_info(layer)).encode())
            for shape in sorted(str(s) for s in cell.shapes(layer).each()):
                result.update(shape.encode() + b'\n')
        for instance in sorted(str((i.cell.name, str(i.cplx_trans), str(i.a), str(i.b), i.na, i.nb))
                               for i in cell.each_inst()):
            result.update(instance.encode() + b'\n')
    return result.hexdigest()


def extend(layout, top, strips):
    if layout.dbu != .001 or not strips:
        raise ValueError('expected nonempty repair on the IHP 1 nm grid')
    well = layout.find_layer(31, 0)
    if well is None:
        raise ValueError('missing NWell')
    before = db.Region(top.begin_shapes_rec(well)).merged()
    signature = preserve_signature(layout, well)
    boundary = top.bbox()
    patches = db.Region()
    for box in strips:
        patch = db.Region(box)
        if before.interacting(patch).count() != 1:
            raise ValueError('repair must touch exactly one connected well')
        patches |= patch
    added = patches - before
    if added.is_empty() or not (added - before.sized(80)).is_empty():
        raise ValueError('repair exceeds the 80 nm well extension')
    # No transistor, well contact, gate, implant or SRAM recognition geometry
    # may be covered by newly added well material.
    for number, datatype in ((1, 0), (5, 0), (6, 0), (14, 0), (25, 0)):
        index = layout.find_layer(number, datatype)
        if index is not None and not (added & db.Region(top.begin_shapes_rec(index))).is_empty():
            raise ValueError('new well overlaps protected device geometry')
    after = (before | patches).merged()
    if after.count() != before.count():
        raise ValueError('repair joined separate well components')
    for box in strips:
        top.shapes(well).insert(box)
    if preserve_signature(layout, well) != signature or top.bbox() != boundary:
        raise ValueError('repair changed other layers, hierarchy or boundary')
    actual = db.Region(top.begin_shapes_rec(well)).merged()
    if not (actual ^ after).is_empty() or not (before - actual).is_empty():
        raise ValueError('unexpected well edit')
    return dict(strips=[str(b) for b in strips], well_components=before.count(),
                added_area_um2=added.area() * layout.dbu**2,
                preserved_other_geometry_sha256=signature)


def build(source, violations, output):
    source, violations, output = map(lambda p: Path(p).resolve(), (source, violations, output))
    hashes = {str(p): digest(p) for p in (source, violations, Path(__file__).resolve())}
    if output.exists():
        raise ValueError('output directory must be new')
    layout = db.Layout()
    layout.read(str(source))
    if len(layout.top_cells()) != 1 or layout.top_cell().name != 'FlowControlledRepeater':
        raise ValueError('expected the complete SRAM/control assembly')
    top = layout.top_cell()
    result = extend(layout, top, corner_strips(violations, top.name, layout.dbu))
    output.mkdir(parents=True, exist_ok=False)
    gds = output / 'assembly.gds'
    layout.write(str(gds))
    reread = db.Layout()
    reread.read(str(gds))
    if preserve_signature(reread, reread.find_layer(31, 0)) != result['preserved_other_geometry_sha256']:
        raise ValueError('GDS serialization changed retained geometry')
    for name, expected in hashes.items():
        if digest(name) != expected:
            raise ValueError('repair input changed')
    result.update(status='candidate_generated', modified_layers=[[31, 0]], source_sha256=hashes,
                  artifact_sha256={str(gds): digest(gds)}, full_chip_qualified=False,
                  drc_qualified=False, lvs_qualified=False)
    (output / 'checks.json').write_text(json.dumps(result, indent=2) + '\n')
    return result


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    for name in ('source', 'violations', 'output'):
        parser.add_argument('--' + name, type=Path, required=True)
    args = parser.parse_args()
    build(args.source, args.violations, args.output)
