"""Reject unsafe well edits and retain all other masks and hierarchy."""
from pathlib import Path
import sys
import tempfile
import unittest

import klayout.db as db

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))
from row_end_wells import corner_strips, extend, preserve_signature


class RowEndWells(unittest.TestCase):
    def setUp(self):
        self.layout = db.Layout()
        self.layout.dbu = .001
        self.top = self.layout.create_cell('TOP')
        self.well = self.layout.layer(31, 0)
        for box in (db.Box(0, 0, 10000, 1000), db.Box(0, 1000, 1000, 3000),
                    db.Box(9000, 1000, 10000, 3000)):
            self.top.shapes(self.well).insert(box)
        self.patch = db.Box(990, 990, 9010, 1080)

    def test_extension_preserves_devices_and_other_masks(self):
        active = self.layout.layer(1, 0)
        self.top.shapes(active).insert(db.Box(100, 500, 9900, 760))
        before = preserve_signature(self.layout, self.well)
        result = extend(self.layout, self.top, [self.patch])
        self.assertEqual(result['well_components'], 1)
        self.assertAlmostEqual(result['added_area_um2'], .64)
        self.assertEqual(preserve_signature(self.layout, self.well), before)

    def test_bridging_independent_wells_rejected(self):
        self.top.shapes(self.well).clear()
        self.top.shapes(self.well).insert(db.Box(0, 0, 4990, 3000))
        self.top.shapes(self.well).insert(db.Box(5010, 0, 10000, 3000))
        with self.assertRaisesRegex(ValueError, 'exactly one'):
            extend(self.layout, self.top, [self.patch])

    def test_device_overlap_rejected_before_mutation(self):
        for layer in (1, 5, 6, 14, 25):
            index = self.layout.layer(layer, 0)
            self.top.shapes(index).insert(db.Box(3000, 1010, 4000, 1070))
            count = self.top.shapes(self.well).size()
            with self.assertRaisesRegex(ValueError, 'protected device'):
                extend(self.layout, self.top, [self.patch])
            self.assertEqual(self.top.shapes(self.well).size(), count)
            self.top.shapes(index).clear()

    def test_excessive_extension_rejected(self):
        with self.assertRaisesRegex(ValueError, '80 nm'):
            extend(self.layout, self.top, [db.Box(990, 990, 9010, 1081)])

    def test_noop_rejected(self):
        with self.assertRaises(ValueError):
            extend(self.layout, self.top, [db.Box(100, 100, 200, 200)])

    def report(self, values, category="'NW.d'"):
        temporary = tempfile.TemporaryDirectory()
        self.addCleanup(temporary.cleanup)
        path = Path(temporary.name) / 'errors.lyrdb'
        path.write_text('<report-database><items>' + ''.join(
            f'<item><category>{category}</category><cell>TOP</cell><values><value>{v}</value></values></item>'
            for v in values) + '</items></report-database>')
        return path

    def test_both_row_orientations(self):
        pairs = ['edge-pair: (1,1.07;1,1)/(1,0.76;1.196,0.76)',
                 'edge-pair: (9,1;9,1.07)/(8.804,0.76;9,0.76)',
                 'edge-pair: (1,4;1,3.93)/(1.196,4.24;1,4.24)',
                 'edge-pair: (9,3.93;9,4)/(9,4.24;8.804,4.24)']
        strips = corner_strips(self.report(pairs), 'TOP', .001)
        self.assertEqual(strips, [self.patch, db.Box(990, 3920, 9010, 4010)])

    def test_unpaired_corner_rejected(self):
        with self.assertRaisesRegex(ValueError, 'unpaired'):
            corner_strips(self.report(['edge-pair: (1,1.07;1,1)/(1,0.76;1.196,0.76)']), 'TOP', .001)

    def test_other_violation_rejected(self):
        with self.assertRaisesRegex(ValueError, 'only top-level'):
            corner_strips(self.report(['irrelevant'], "'NW.a'"), 'TOP', .001)

    def test_wrong_corner_dimensions_rejected(self):
        with self.assertRaisesRegex(ValueError, '70 nm'):
            corner_strips(self.report(['edge-pair: (1,1.08;1,1)/(1,0.76;1.196,0.76)']), 'TOP', .001)


if __name__ == '__main__':
    unittest.main()
