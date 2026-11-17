#!/usr/bin/env python3
"""Guard the semantic names used on the SAR slice mask image."""

import json
import unittest
from pathlib import Path

from element_names import names_for, source_hashes


HERE = Path(__file__).resolve().parent


class ElementNames(unittest.TestCase):
    def test_all_placements_have_the_same_complete_name_map(self):
        mapping = json.loads((HERE / "element_names.json").read_text())
        self.assertEqual(mapping["model_source_sha256"], source_hashes())
        self.assertEqual(len(mapping["elements"]), 137)
        self.assertEqual(len({item["hierarchical_name"] for item in mapping["elements"]}), 137)
        placements = sorted(HERE.rglob("adc_sar_slice.placement.json"))
        self.assertEqual(len(placements), 27)
        for placement in placements:
            with self.subTest(placement=placement):
                self.assertEqual(names_for(json.loads(placement.read_text())), mapping["elements"])

    def test_important_names_state_their_roles(self):
        mapping = json.loads((HERE / "element_names.json").read_text())
        names = {item["physical_name"]: item["hierarchical_name"] for item in mapping["elements"]}
        self.assertEqual(names["D3"], "sampler/C0_capture")
        self.assertEqual(names["D47"], "ladder/R0_termination_2r")
        self.assertEqual(names["D80"], "comparison/M2_evaluate_tail")
        self.assertEqual(names["D141"], "interface/C6_decision_output")


if __name__ == "__main__":
    unittest.main()
