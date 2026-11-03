"""Guard hierarchical area, multiplicity and the distinction from signoff."""
import importlib.util
from pathlib import Path
import tempfile
import unittest

import klayout.db as db

spec = importlib.util.spec_from_file_location('budget', Path(__file__).resolve().parents[1] / 'mim_area_budget.py')
area = importlib.util.module_from_spec(spec)
spec.loader.exec_module(area)


class AreaBudgetTests(unittest.TestCase):
    def setUp(self):
        self.directory = tempfile.TemporaryDirectory()
        self.addCleanup(self.directory.cleanup)
        self.path = Path(self.directory.name) / 'layout.gds'
        layout = db.Layout()
        layout.dbu = .001
        self.layout = layout
        self.top = layout.create_cell('Chip')
        capacitor = layout.create_cell('Capacitor')
        capacitor.shapes(layout.layer(36, 0)).insert(db.Box(0, 0, 1000, 2000))
        # Coincident shapes must not double the area; arrays must multiply it.
        capacitor.shapes(layout.layer(36, 0)).insert(db.Box(0, 0, 1000, 2000))
        self.top.insert(db.CellInstArray(capacitor.cell_index(), db.Trans(),
                                        db.Vector(3000, 0), db.Vector(0, 4000), 3, 2))
        layout.write(str(self.path))
        self.components = [dict(name='macro', gds=str(self.path), copies=8)]

    def test_nested_array_and_overlapping_shapes(self):
        self.assertEqual(area.mim_area(self.path)['mim_area_um2'], 12)
        self.assertEqual(area.budget(self.components, 95, 'component_subtotal')['total_area_um2'], 96)

    def test_subtotal_does_not_claim_full_chip_pass(self):
        result = area.budget(self.components, 100, 'component_subtotal')
        self.assertEqual(result['status'], 'subtotal_within_recommended_area')
        self.assertFalse(result['drc_signoff'])
        with self.assertRaises(ValueError):
            area.budget(self.components, 100, 'chip_layout')

    def test_recommendation_boundary(self):
        self.components[0]['copies'] = 1
        self.assertEqual(area.budget(self.components, 12, 'chip_layout')['status'], 'within_recommended_area')
        self.assertEqual(area.budget(self.components, 11, 'chip_layout')['status'], 'exceeds_recommended_area')

    def test_invalid_counts_and_duplicate_names(self):
        for count in (0, -1, .5, True):
            with self.assertRaises(ValueError):
                area.budget([{**self.components[0], 'copies': count}], 100, 'component_subtotal')
        with self.assertRaises(ValueError):
            area.budget(self.components * 2, 100, 'component_subtotal')

    def test_top_selection(self):
        self.layout.create_cell('Unrelated')
        self.layout.write(str(self.path))
        with self.assertRaises(ValueError):
            area.mim_area(self.path)
        self.assertEqual(area.mim_area(self.path, 'Chip')['mim_area_um2'], 12)
        with self.assertRaises(ValueError):
            area.mim_area(self.path, 'Missing')

    def test_no_mim_is_valid(self):
        layout = db.Layout()
        layout.create_cell('Digital')
        layout.write(str(self.path))
        self.assertEqual(area.mim_area(self.path)['mim_area_um2'], 0)


if __name__ == '__main__':
    unittest.main()
