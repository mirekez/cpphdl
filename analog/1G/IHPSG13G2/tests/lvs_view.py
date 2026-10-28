"""Verify that flat LVS label scoping cannot silently change mask geometry."""
from pathlib import Path
import sys
import tempfile
import unittest
import klayout.db as db

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))
from lvs_view import create_flat_view, mask_signature


class LabelScoping(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory()
        self.addCleanup(self.temp.cleanup)
        self.root = Path(self.temp.name)
        self.layout = db.Layout()
        self.layout.dbu = .001
        self.top = self.layout.create_cell('Top')
        self.child = self.layout.create_cell('Device')
        self.metal = self.layout.layer(10, 0)
        self.pin = self.layout.layer(10, 25)
        self.model = self.layout.layer(51, 0)
        self.child.shapes(self.metal).insert(db.Box(0, 0, 2000, 200))
        self.child.shapes(self.pin).insert(db.Text('D', db.Trans(0, 0)))
        self.child.shapes(self.model).insert(db.Text('nmos', db.Trans(0, 0)))
        self.top.insert(db.CellInstArray(self.child.cell_index(), db.Trans(1000, 2000)))
        self.top.shapes(self.pin).insert(db.Text('OUT', db.Trans(1000, 2000)))
        self.source, self.target = self.root / 'source.gds', self.root / 'view.gds'

    def test_masks_hierarchy_top_ports_and_model_labels_survive(self):
        self.layout.write(str(self.source))
        report = create_flat_view(self.source, self.target, 'Top')
        checked = db.Layout()
        checked.read(str(self.target))
        self.assertEqual(len(report['removed_local_pin_labels']), 1)
        self.assertEqual(mask_signature(checked, 'Top'), mask_signature(self.layout, 'Top'))
        self.assertEqual([s.text.string for s in checked.cell('Top').each_shape(checked.layer(10, 25))], ['OUT'])
        self.assertEqual([s.text.string for s in checked.cell('Device').each_shape(checked.layer(51, 0))], ['nmos'])

    def test_mask_or_top_label_changes_are_detected(self):
        original = mask_signature(self.layout, 'Top')
        self.child.shapes(self.metal).insert(db.Box(0, 500, 2000, 700))
        self.assertNotEqual(mask_signature(self.layout, 'Top'), original)
        changed = mask_signature(self.layout, 'Top')
        self.top.shapes(self.pin).clear()
        self.assertNotEqual(mask_signature(self.layout, 'Top'), changed)

    def test_unknown_local_labels_require_review(self):
        self.child.shapes(self.pin).insert(db.Text('UNEXPECTED_PORT', db.Trans(0, 0)))
        self.layout.write(str(self.source))
        with self.assertRaisesRegex(ValueError, 'unknown local device pin'):
            create_flat_view(self.source, self.target, 'Top')


if __name__ == '__main__':
    unittest.main()
