"""Exercise structural hints separately from the final connectivity audit."""
from pathlib import Path
import importlib.util
import sys
import tempfile
import unittest

import klayout.db as db

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))
from sram_lvs_compare import Correspondence

spec = importlib.util.spec_from_file_location('audit_fixture', Path(__file__).with_name('sram_lvs_audit.py'))
fixture = importlib.util.module_from_spec(spec)
spec.loader.exec_module(fixture)
Evidence, PORTS, SPICE = fixture.Evidence, fixture.PORTS, fixture.SPICE


class ComparableEvidence(Evidence):
    def compare(self, comparer):
        return comparer.compare(self.layout, self.reference, self.matches)


class StructuralComparison(unittest.TestCase):
    def setUp(self):
        self.directory = tempfile.TemporaryDirectory()
        self.addCleanup(self.directory.cleanup)
        path = Path(self.directory.name) / 'fixture.cir'
        path.write_text(SPICE)
        self.evidence = ComparableEvidence(path)
        self.engine = Correspondence(self.evidence, 'TOP')

    def test_complete_comparison(self):
        result = self.engine.run(PORTS, {'res_metal2': .088})
        self.assertEqual(result['status'], 'graph_correspondence_pass')
        self.assertEqual(result['independent_audit']['counts']['devices'], 3)
        self.assertFalse(result['full_sram_qualified'])

    def test_repeated_comparison_has_no_duplicate_circuits(self):
        for _ in range(2):
            result = self.engine.run(PORTS, {'res_metal2': .088})
            self.assertEqual(result['independent_audit']['counts']['circuits'], 2)

    def test_wrong_child_connection_rejected(self):
        circuit = self.evidence.layout.circuit_by_name('TOP')
        child = next(circuit.each_subcircuit())
        child.connect_pin(child.circuit_ref().pin_by_name('A').id(), circuit.net_by_name('VSS'))
        with self.assertRaises(ValueError):
            self.engine.run(PORTS, {'res_metal2': .088})

    def test_resistance_is_checked_after_structural_matching(self):
        # R is intentionally absent from the coloring key: geometry-defined
        # references need the independent extraction-model check at the end.
        device = next(self.evidence.layout.circuit_by_name('TOP').each_device())
        device.set_parameter('R', 0)
        with self.assertRaises(ValueError):
            self.engine.run(PORTS, {'res_metal2': .088})

    def test_unexpected_column_inventory_rejected(self):
        with self.assertRaisesRegex(ValueError, 'boundary inventory'):
            self.engine.expand_column_logic()

    def test_residual_search_budget_is_enforced(self):
        with self.assertRaisesRegex(ValueError, 'search budget'):
            self.engine.residual(maximum_steps=0)


if __name__ == '__main__':
    unittest.main()
