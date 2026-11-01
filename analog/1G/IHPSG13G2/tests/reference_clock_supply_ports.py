#!/usr/bin/env python3
"""Keep power-pin promotion attached to real metal across GDS serialization."""
from pathlib import Path
import sys
import tempfile
import unittest

import klayout.db as db

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))
from reference_clock_supply_ports import promote_supply_ports


class SupplyPins(unittest.TestCase):
    def fixture(self, polygons=False):
        layout = db.Layout()
        layout.dbu = .001
        top = layout.create_cell('PowerPinFixture')
        info = dict(upper_ground_straps=[{}, {}], terminals={})
        for index, (net, target) in enumerate((('VSS', 134), ('VLOGIC', 126), ('VDD', 126))):
            x, y = index * 10000, 0
            pin = db.Box(x - 160, y - 160, x + 160, y + 160)
            for layer in (67, target):
                top.shapes(layout.layer(layer, 0)).insert(db.Box(x - 1000, -1000, x + 1000, 1000))
            top.shapes(layout.layer(67, 2)).insert(db.Polygon(pin) if polygons else pin)
            top.shapes(layout.layer(67, 25)).insert(db.Text(net, db.Trans(x, y)))
            info['terminals'][net] = dict(layer=67, x_um=x * layout.dbu, y_um=0)
        return layout, top, info

    def test_pin_survives_gds_roundtrip_without_metal_changes(self):
        for polygons in (False, True):
            with self.subTest(polygons=polygons):
                layout, top, info = self.fixture(polygons)
                before = {layer: db.Region(top.begin_shapes_rec(layout.layer(layer, 0)))
                          for layer in (67, 126, 134)}
                promote_supply_ports(layout, top, info)
                with tempfile.TemporaryDirectory() as directory:
                    path = Path(directory) / 'pins.gds'
                    layout.write(str(path))
                    restored = db.Layout()
                    restored.read(str(path))
                    cell = restored.top_cell()
                    for layer, metal in before.items():
                        self.assertTrue((db.Region(cell.begin_shapes_rec(restored.layer(layer, 0))) ^ metal).is_empty())
                    self.assertTrue(cell.shapes(restored.layer(67, 2)).is_empty())
                    self.assertTrue(cell.shapes(restored.layer(67, 25)).is_empty())
                    for name, port in info['terminals'].items():
                        labels = [s.text.string for s in cell.shapes(restored.layer(port['layer'], 25)).each()
                                  if s.is_text()]
                        self.assertEqual(labels.count(name), 1)
                        point = db.Point(round(port['x_um'] / restored.dbu), 0)
                        pins = [s for s in cell.shapes(restored.layer(port['layer'], 2)).each()
                                if s.bbox().contains(point)]
                        self.assertEqual(len(pins), 1)

    def test_missing_upper_conductor(self):
        layout, top, info = self.fixture()
        top.shapes(layout.layer(134, 0)).clear()
        with self.assertRaisesRegex(ValueError, 'lacks a physical conductor'):
            promote_supply_ports(layout, top, info)

    def test_partially_supported_pin(self):
        layout, top, info = self.fixture()
        top.shapes(layout.layer(134, 0)).clear()
        top.shapes(layout.layer(134, 0)).insert(db.Box(-1000, -1000, 0, 1000))
        with self.assertRaisesRegex(ValueError, 'lacks a physical conductor'):
            promote_supply_ports(layout, top, info)

    def test_ambiguous_label(self):
        layout, top, info = self.fixture()
        top.shapes(layout.layer(67, 25)).insert(db.Text('VSS', db.Trans(500, 0)))
        with self.assertRaisesRegex(ValueError, 'ambiguous original supply pin'):
            promote_supply_ports(layout, top, info)

    def test_missing_physical_ground_straps(self):
        layout, top, info = self.fixture()
        info['upper_ground_straps'] = []
        with self.assertRaisesRegex(ValueError, 'upper ground straps'):
            promote_supply_ports(layout, top, info)

    def test_repeated_promotion_rejected(self):
        layout, top, info = self.fixture()
        promote_supply_ports(layout, top, info)
        with self.assertRaisesRegex(ValueError, 'original supply pin layer'):
            promote_supply_ports(layout, top, info)


if __name__ == '__main__':
    unittest.main()
