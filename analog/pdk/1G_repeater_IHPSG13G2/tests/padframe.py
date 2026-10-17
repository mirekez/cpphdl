#!/usr/bin/env python3
"""Check actual pad cells, pinout, power abutment and missing-pad rejection."""
import argparse
import importlib.util
import json
from pathlib import Path
import re
import unittest
from unittest.mock import patch

SOURCE = Path(__file__).resolve().parents[1]
spec = importlib.util.spec_from_file_location("repeater_padframe", SOURCE / "padframe.py")
padframe = importlib.util.module_from_spec(spec)
spec.loader.exec_module(padframe)


class Padframe(unittest.TestCase):
    def test_real_pads_and_esd(self):
        result = padframe.assemble(args.pdk.resolve(), args.work.resolve())
        self.assertEqual(result["cells"]["sg13g2_IOPadAnalog"], 16)
        self.assertEqual(result["cells"]["sg13g2_Corner"], 4)
        self.assertEqual(len(result["placements"]), 36)
        self.assertEqual(len(result["ports"]), 44)
        self.assertEqual(result["abutment_rail_components"], dict.fromkeys(padframe.RAILS, 1))
        self.assertFalse(result["full_1000base_t_chip"])
        self.assertFalse(result["seal_ring_present"])
        library = (args.pdk / "ihp-sg13g2/libs.ref/sg13g2_io/cdl/sg13g2_io.cdl").read_text()
        analog = re.search(r"\.SUBCKT sg13g2_IOPadAnalog .*?\.ENDS", library, re.S)[0]
        for device in ("sg13g2_Clamp_P20N0D", "sg13g2_Clamp_N20N0D", "sg13g2_SecondaryProtection"):
            self.assertIn(device, analog)
        for cell in ("sg13g2_IOPadVdd", "sg13g2_IOPadIOVdd"):
            supply = re.search(r"\.SUBCKT " + cell + r" .*?\.ENDS", library, re.S)[0]
            self.assertIn("sg13g2_RCClampInverter", supply)
            self.assertIn("sg13g2_Clamp_N43N43D4R", supply)
        self.assertEqual(json.loads((args.work / "interface.json").read_text()), result["ports"])

    def test_missing_pad_is_a_physical_gap(self):
        plan = padframe.pad_plan()
        plan["south"].pop()
        with patch.object(padframe, "pad_plan", return_value=plan):
            with self.assertRaisesRegex(ValueError, "gap"):
                padframe.assemble(args.pdk.resolve(), args.work.resolve() / "negative")


if __name__ == "__main__":
    parser = argparse.ArgumentParser()
    parser.add_argument("--pdk", type=Path, required=True)
    parser.add_argument("--work", type=Path, required=True)
    args, extra = parser.parse_known_args()
    unittest.main(argv=[__file__, *extra])
