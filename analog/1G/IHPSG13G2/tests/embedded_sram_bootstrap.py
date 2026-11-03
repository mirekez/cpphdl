"""Leaf seeding cannot bypass complete SRAM or enclosing-circuit verification."""
import importlib.util
from pathlib import Path
import sys
import unittest

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))
from embedded_sram_bootstrap import BootstrappedSramCorrespondence

spec = importlib.util.spec_from_file_location('assembly_fixture', Path(__file__).with_name('embedded_sram_lvs.py'))
fixture = importlib.util.module_from_spec(spec)
spec.loader.exec_module(fixture)


class BootstrapTests(fixture.AssemblyTests):
    def setUp(self):
        super().setUp()
        self.engine = BootstrappedSramCorrespondence(self.evidence, 'TOP', 'CHIP')
        self.seed = self.engine.seed_leaves()

    def test_seed_is_explicitly_unqualified(self):
        self.assertFalse(self.seed['qualified'])
        self.assertEqual(self.seed['matched_leaf_circuits'], 1)
        self.assertEqual(self.seed['deferred_circuits'], ['CHIP', 'TOP'])
        self.assertTrue(self.engine.leaves_only)
        self.run_assembly()
        self.assertFalse(self.engine.leaves_only)
        self.assertEqual(set(self.engine.circuit_pairs()), {'CHIP', 'TOP', 'INV'})

    def test_bad_leaf_cannot_seed(self):
        circuit = self.evidence.layout.circuit_by_name('INV')
        next(circuit.each_device()).connect_terminal('G', circuit.net_by_name('VSS'))
        with self.assertRaises(ValueError):
            self.engine.seed_leaves()

    def test_changed_sram_resistor_is_not_hidden_by_seed(self):
        circuit = self.evidence.layout.circuit_by_name('TOP')
        next(circuit.each_device()).set_parameter('R', 0)
        with self.assertRaises(ValueError):
            self.run_assembly()


if __name__ == '__main__':
    unittest.main()
