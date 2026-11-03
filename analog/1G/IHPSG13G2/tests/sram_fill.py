"""Check upper-metal fill isolation, exclusions and input guards."""
from pathlib import Path
import sys
import unittest

import klayout.db as db

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))
from sram_fill import add_fill, PATTERNS


class FillGuards(unittest.TestCase):
    def fixture(self):
        layout = db.Layout()
        layout.dbu = .001
        top = layout.create_cell('test')
        top.shapes(layout.layer(16, 0)).insert(db.Box(0, 0, 200000, 200000))
        return layout, top

    def test_exclusions_and_geometry(self):
        layout, top = self.fixture()
        top.shapes(layout.layer(160, 0)).insert(db.Box(0, 0, 10000, 200000))
        top.shapes(layout.layer(26, 0)).insert(db.Box(90000, 90000, 100000, 100000))
        original = {info: db.Region(top.begin_shapes_rec(layout.layer(info)))
                    for info in layout.layer_infos()}
        result = add_fill(layout, top)
        for info, region in original.items():
            self.assertTrue((region ^ db.Region(top.begin_shapes_rec(layout.layer(info)))).is_empty())
        for number, _, _, gap, _, transistor_gap, minimum, maximum in PATTERNS:
            fill = db.Region(top.begin_shapes_rec(layout.layer(number, 22)))
            self.assertTrue(fill.space_check(round(gap / layout.dbu)).is_empty())
            blocked = db.Region(top.begin_shapes_rec(layout.layer(26, 0))).sized(round(transistor_gap / layout.dbu))
            blocked |= db.Region(top.begin_shapes_rec(layout.layer(160, 0)))
            self.assertTrue((fill & blocked).is_empty())
            self.assertLessEqual(minimum, result[str(number)]['density_percent'])
            self.assertGreaterEqual(maximum, result[str(number)]['density_percent'])
        with self.assertRaises(ValueError): add_fill(layout, top)

    def test_existing_metal_rejected_before_mutation(self):
        for number, *_ in PATTERNS:
            layout, top = self.fixture()
            top.shapes(layout.layer(number, 0)).insert(db.Box(0, 0, 1000, 1000))
            with self.assertRaises(ValueError): add_fill(layout, top)
            self.assertEqual(layout.cells(), 1)


if __name__ == '__main__':
    unittest.main()
