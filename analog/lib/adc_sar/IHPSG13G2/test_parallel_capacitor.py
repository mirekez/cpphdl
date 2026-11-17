#!/usr/bin/env python3
"""Guard the two-PCell storage layout against illegal narrow plate bridges."""
import argparse
import sys
import unittest
from pathlib import Path

import klayout.db as db

sys.path.insert(0, str(Path(__file__).resolve().parents[3] / '1G/IHPSG13G2'))
from parallel_capacitor_layout import append_mim_parallel


class ParallelMim(unittest.TestCase):
    pdk = None

    def test_two_real_cells_and_legal_bridge(self):
        layout = db.Layout()
        layout.dbu = .001
        cell, info = append_mim_parallel(layout, self.pdk, 'hold10', 10e-12)
        self.assertEqual(info['configuration'], 'parallel')
        self.assertEqual(info['capacitors'], 2)
        self.assertEqual(len(list(cell.each_inst())), 2)
        self.assertGreaterEqual(info['pcell_nominal_capacitance_F'], 10e-12)
        self.assertLessEqual(info['pcell_nominal_capacitance_F'], 10.1e-12)
        self.assertIn('Xleft PLUS MINUS', info['schematic'])
        self.assertIn('Xright PLUS MINUS', info['schematic'])
        self.assertNotEqual(info['terminals']['PLUS']['layer'], info['terminals']['MINUS']['layer'])
        for layer in (67, 126):
            shapes = list(cell.shapes(layout.layer(layer, 0)).each())
            self.assertEqual(len(shapes), 1)
            self.assertGreaterEqual(shapes[0].bbox().height() * layout.dbu, 1.64)

    def test_range_is_explicit(self):
        layout = db.Layout()
        layout.dbu = .001
        with self.assertRaises(ValueError):
            append_mim_parallel(layout, self.pdk, 'bad', 8e-12)


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--pdk', type=Path, required=True)
    args = parser.parse_args()
    ParallelMim.pdk = args.pdk.resolve()
    unittest.main(argv=[sys.argv[0]])
