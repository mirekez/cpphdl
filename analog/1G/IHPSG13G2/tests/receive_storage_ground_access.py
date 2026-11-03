#!/usr/bin/env python3
"""Reject storage straps that reach VSS only through the old lower-metal escape."""
import argparse
import copy
import json
import math
from pathlib import Path
import sys
import unittest

import klayout.db as db

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))
from extract_analog import digest


def connected_returns(region, geometry, dbu):
    pin = geometry['terminals']['VSS']
    banks = geometry.get('storage_access', {}).get('banks', [])
    if pin['layer'] != 134 or not banks or not math.isclose(dbu, .001):
        raise ValueError('reviewed TopMetal2 ground pin and storage banks required')

    def point(x, y):
        if not all(math.isfinite(v) for v in (x, y)):
            raise ValueError('nonfinite ground access')
        return db.Point(round(x / dbu), round(y / dbu))

    origin = point(pin['x_um'], pin['y_um'])
    merged = region.merged()
    polygons = [p.dup() for p in merged.each() if p.inside(origin)]
    if len(polygons) != 1:
        raise ValueError('ground pin must lie inside one upper-metal conductor')
    for bank in banks:
        rows = bank['row_y_um']
        if len(rows) != bank['rows'] or not rows:
            raise ValueError('changed storage return row inventory')
        for y in rows:
            if not polygons[0].inside(point(bank['ground_column_um'], y)):
                raise ValueError('no direct upper-metal ground path: ' + bank['component'])


def fixture(path):
    proof = json.loads(path.read_text())
    if proof.get('status') != 'pass' or not proof.get('missing_device_rejected'):
        raise ValueError('verified storage layout required')
    for group in ('source_sha256', 'artifact_sha256'):
        if not proof.get(group):
            raise ValueError('missing geometry evidence')
        for name, expected in proof[group].items():
            if digest(name) != expected:
                raise ValueError('changed geometry input: ' + name)
    layout = db.Layout()
    layout.read(str(Path(proof['work']) / 'reference.gds'))
    region = db.Region(layout.top_cell().begin_shapes_rec(layout.layer(134, 0)))
    # Recursive regions retain their layout owner until materialized.
    return layout, region, proof['geometry'], layout.dbu


class GroundAccess(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.layout, cls.region, cls.geometry, cls.dbu = fixture(args.layout)

    def test_every_declared_return_reaches_upper_ground_rail(self):
        connected_returns(self.region, self.geometry, self.dbu)

    def test_interrupted_upper_return_is_rejected(self):
        # Lower layers remain untouched: ordinary LVS can still see VSS
        # through their original long escape. The intended upper path fails.
        bank = self.geometry['storage_access']['banks'][0]
        gy = self.geometry['terminals']['VSS']['y_um']
        y = round((gy + min(bank['row_y_um'])) / (2 * self.dbu))
        bounds = self.region.bbox()
        gap = db.Region(db.Box(bounds.left - 1, y - 100, bounds.right + 1, y + 100))
        with self.assertRaisesRegex(ValueError, 'no direct upper-metal ground path'):
            connected_returns(self.region - gap, self.geometry, self.dbu)

    def test_wrong_pin_layer_is_rejected(self):
        geometry = copy.deepcopy(self.geometry)
        geometry['terminals']['VSS']['layer'] = 126
        with self.assertRaises(ValueError):
            connected_returns(self.region, geometry, self.dbu)

    def test_missing_storage_inventory_is_rejected(self):
        geometry = copy.deepcopy(self.geometry)
        geometry['storage_access']['banks'] = []
        with self.assertRaises(ValueError):
            connected_returns(self.region, geometry, self.dbu)


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--layout', type=Path, required=True)
    args, remaining = parser.parse_known_args()
    unittest.main(argv=[sys.argv[0]] + remaining)
