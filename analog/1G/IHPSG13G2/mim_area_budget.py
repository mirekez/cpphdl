"""Measure the PDK's recommended chip MIM budget from actual hierarchical GDS.

A component inventory is a planning subtotal. Only a final chip layout can
provide the complete area, and neither mode replaces the PDK DRC run.
"""
import argparse
import hashlib
import json
import math
from pathlib import Path

import klayout.db as db


def digest(path):
    return hashlib.sha256(Path(path).read_bytes()).hexdigest()


def mim_area(path, top=None):
    layout = db.Layout()
    layout.read(str(path))
    if top is None:
        cells = list(layout.top_cells())
        if len(cells) != 1:
            raise ValueError('select a top cell for a multi-top layout')
        cell = cells[0]
    else:
        cell = layout.cell(top)
        if cell is None:
            raise ValueError('requested top cell is absent')
    # Merge repeated/overlapping drawing shapes like the physical DRC layer.
    # Recursive traversal includes array instances and their transformations.
    layer = layout.find_layer(36, 0)
    area = 0.0 if layer is None else db.Region(cell.begin_shapes_rec(layer)).merged().area() * layout.dbu ** 2
    return dict(top=cell.name, mim_area_um2=area)


def budget(components, limit, scope):
    if scope not in ('component_subtotal', 'chip_layout'):
        raise ValueError('unknown area budget scope')
    if not math.isfinite(limit) or limit <= 0 or not components:
        raise ValueError('a positive finite PDK limit and nonempty inventory are required')
    rows, names, hashes = [], set(), {}
    for component in components:
        name, path = component['name'], Path(component['gds']).resolve()
        count = component['copies']
        if (not isinstance(name, str) or not name or name in names or
                type(count) is not int or count <= 0):
            raise ValueError('unique component names and positive integer copy counts are required')
        names.add(name)
        before = digest(path)
        measured = mim_area(path, component.get('top'))
        if digest(path) != before:
            raise ValueError('layout changed during area measurement')
        hashes[str(path)] = before
        rows.append(dict(name=name, gds=str(path), copies=count, **measured,
                         total_area_um2=count * measured['mim_area_um2']))
    if scope == 'chip_layout' and (len(rows) != 1 or rows[0]['copies'] != 1):
        raise ValueError('chip scope requires one assembled layout, not component multiplication')
    total = math.fsum(row['total_area_um2'] for row in rows)
    exceeded = total > limit
    status = ('exceeds_recommended_area' if exceeded else
              'within_recommended_area' if scope == 'chip_layout' else 'subtotal_within_recommended_area')
    return dict(status=status, scope=scope, rule='MIM.gR', rule_kind='recommended',
                layer=[36, 0], recommended_limit_um2=limit, total_area_um2=total,
                remaining_area_um2=limit - total, components=rows, input_sha256=hashes,
                drc_signoff=False)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--pdk', type=Path, required=True)
    parser.add_argument('--inventory', type=Path, required=True,
                        help='JSON: scope and components [{name, gds, copies, optional top}]; paths relative to inventory')
    parser.add_argument('--output', type=Path, required=True)
    args = parser.parse_args()
    inventory = json.loads(args.inventory.read_text())
    rules = args.pdk / 'ihp-sg13g2/libs.tech/klayout/tech/drc/rule_decks/sg13g2_tech_default.json'
    content = json.loads(rules.read_text())
    values = [section['Mim_gR'] for section in content.values()
              if isinstance(section, dict) and 'Mim_gR' in section]
    if len(values) != 1:
        raise ValueError('ambiguous or missing PDK MIM.gR limit')
    components = [{**row, 'gds': str(args.inventory.resolve().parent / row['gds'])}
                  for row in inventory['components']]
    result = budget(components, float(values[0]), inventory['scope'])
    result['input_sha256'].update({str(path.resolve()): digest(path)
                                  for path in (rules, args.inventory, Path(__file__))})
    args.output.parent.mkdir(parents=True, exist_ok=True)
    args.output.write_text(json.dumps(result, indent=2) + '\n')
    print(f"{result['status']}: {result['total_area_um2']:.4f} / {values[0]} um2 ({result['scope']})")
    return 1 if result['status'] == 'exceeds_recommended_area' else 0


if __name__ == '__main__':
    raise SystemExit(main())
