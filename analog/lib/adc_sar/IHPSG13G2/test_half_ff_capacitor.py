#!/usr/bin/env python3
"""Guard the six-PCell series element used by the latch Miller trial."""

import argparse
import sys
import unittest
from pathlib import Path

import klayout.db as db

sys.path.insert(0, str(Path(__file__).resolve().parents[3] / "1G/IHPSG13G2"))
from half_ff_capacitor_layout import append_half_ff


class HalfFemtofaradMim(unittest.TestCase):
    pdk = None

    def test_six_real_plates_and_five_distinct_junctions(self):
        layout = db.Layout()
        layout.dbu = .001
        cell, info = append_half_ff(layout, self.pdk, "latch_half", .5e-15)
        self.assertEqual(info["capacitors"], 6)
        self.assertEqual(len(list(cell.each_inst())), 6)
        self.assertEqual(len(info["junction_vias_um"]), 5)
        self.assertEqual(len(set(tuple(p) for p in info["junction_vias_um"])), 5)
        self.assertGreaterEqual(info["pcell_nominal_capacitance_F"], .5e-15)
        self.assertLessEqual(info["pcell_nominal_capacitance_F"], .505e-15)
        for index in range(6):
            self.assertIn(f"X{index} ", info["schematic"])
        self.assertNotEqual(info["terminals"]["PLUS"]["layer"],
                            info["terminals"]["MINUS"]["layer"])
        self.assertIn("MID4 MINUS", info["schematic"])

    def test_other_value_rejected(self):
        layout = db.Layout()
        layout.dbu = .001
        with self.assertRaises(ValueError):
            append_half_ff(layout, self.pdk, "wrong", 1e-15)


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--pdk", type=Path, required=True)
    args = parser.parse_args()
    HalfFemtofaradMim.pdk = args.pdk.resolve()
    unittest.main(argv=[sys.argv[0]])
