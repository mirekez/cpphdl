"""Partition must preserve node aliases, contributions, storage and driver topology."""
from pathlib import Path
import sys
import tempfile
import unittest

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))
from hierarchy import partition, experiment

MODEL = '''`include "disciplines.vams"
module Pair(Vdd, Vss, A, B, Y, Z);
    inout Vdd, Vss, A, B, Y, Z;
    electrical Vdd, Vss, A, B, Y, Z;
    analog begin
        // device 0, IC first, first.hpp:1
        I(Y, Vdd) <+ V(A, Vdd) / 1000.0;
        // device 1, IC first, first.hpp:2
        I(Y, Vss) <+ V(A, Vss) / 2000.0;
        // device 2, IC second, second.hpp:1
        I(Z, Vdd) <+ V(B, Vdd) / 1000.0;
        // device 3, IC second, second.hpp:2
        I(Z, Vss) <+ V(B, Vss) / 2000.0;
        // device 4, IC storage, storage.hpp:1
        I(Y, Z) <+ 1e-12 * ddt(V(Y, Z));
    end
endmodule
'''
DECK = '''Shared circuit
.model CircuitModel Pair
Ndesign supply 0 a b y z CircuitModel
Cexternal y z 1e-12 IC=0
.control
pre_osdi design.osdi
tran 1n 10n uic
.endc
.end
'''


class Partition(unittest.TestCase):
    def split(self, model=MODEL):
        directory = tempfile.TemporaryDirectory()
        self.addCleanup(directory.cleanup)
        work = Path(directory.name)
        return partition(model, work), work

    def test_equal_ic_equations_share_a_model_and_preserve_cut_nodes(self):
        result, work = self.split()
        self.assertEqual(result['contribution_count'], 5)
        self.assertEqual(len(result['instances']), 3)
        self.assertEqual(len(result['masters']), 2)
        first, second, storage = result['instances']
        self.assertEqual(first['master'], second['master'])
        self.assertEqual(first['nodes'], ['Y', 'Vdd', 'A', 'Vss'])
        self.assertEqual(second['nodes'], ['Z', 'Vdd', 'B', 'Vss'])
        self.assertEqual(storage['nodes'], ['Y', 'Z'])
        self.assertIn('ddt(V(p0, p1))', (work / result['masters'][storage['master']]['file']).read_text())

    def test_experiment_preserves_sources_and_external_stored_charge(self):
        result, work = self.split()
        deck = experiment(DECK, result, work)
        self.assertIn('Xhierarchy supply 0 a b y z Pair', deck)
        self.assertIn('Cexternal y z 1e-12 IC=0', deck)
        self.assertEqual(deck.count('pre_osdi '), 2)
        self.assertNotIn('Ndesign ', deck)
        self.assertNotIn('.model CircuitModel', deck)

    def test_duplicate_device_identity_is_rejected(self):
        with self.assertRaisesRegex(ValueError, 'identity'):
            self.split(MODEL.replace('// device 4,', '// device 3,'))

    def test_both_simulation_driver_branches_preserve_their_rails_and_control(self):
        old = 'I(Y, Vdd) <+ V(A, Vdd) / 1000.0;'
        branches = ('I(Y, Vss) <+ (1.0 - V(A)) * V(Y, Vss) / 50.0; // sim_driver\n'
                    '        I(Y, Vdd) <+ V(A) * V(Y, Vdd) / 50.0; // sim_driver')
        result, work = self.split(MODEL.replace(old, branches))
        self.assertEqual(result['contribution_count'], 6)
        first = result['instances'][0]
        model = (work / result['masters'][first['master']]['file']).read_text()
        self.assertIn('(1.0 - V(', model)
        self.assertEqual(model.count('/ 50.0;'), 2)
        for changed in (branches.splitlines()[0], branches.replace('V(A) *', 'V(B) *'),
                        branches.replace('V(Y, Vdd) / 50.0', 'V(Y, Vdd) / 100.0')):
            with self.subTest(branches=changed), self.assertRaisesRegex(ValueError, 'paired simulation driver'):
                self.split(MODEL.replace(old, changed))

    def test_undeclared_node_is_rejected(self):
        with self.assertRaisesRegex(ValueError, 'undeclared'):
            self.split(MODEL.replace('V(A, Vdd)', 'V(MISSING, Vdd)'))

    def test_case_sensitive_nodes_cannot_be_merged_by_spice(self):
        with self.assertRaisesRegex(ValueError, 'letter case'):
            self.split(MODEL.replace('electrical Vdd,', 'electrical vdd, Vdd,'))

    def test_cross_instance_current_probe_is_rejected(self):
        with self.assertRaisesRegex(ValueError, 'branch-current'):
            self.split(MODEL.replace('V(A, Vdd)', 'I(Z, Vss)'))

    def test_ideal_voltage_statement_and_anonymous_law_are_rejected(self):
        for changed in (MODEL.replace('I(Y, Vdd) <+', 'V(Y, Vdd) <+'),
                        MODEL.replace('// device 0, IC first, first.hpp:1', '// anonymous law')):
            with self.subTest(model=changed), self.assertRaisesRegex(ValueError, 'current contribution'):
                self.split(changed)

    def test_hidden_module_state_is_rejected(self):
        with self.assertRaisesRegex(ValueError, 'declarations'):
            self.split(MODEL.replace('    analog begin', '    real hidden;\n    analog begin'))

    def test_bad_experiment_port_count_or_extra_model_load_is_rejected(self):
        result, work = self.split()
        for changed in (DECK.replace('a b y z CircuitModel', 'a y z CircuitModel'),
                        DECK.replace('pre_osdi design.osdi', 'pre_osdi design.osdi\npre_osdi other.osdi')):
            with self.subTest(deck=changed), self.assertRaises(ValueError):
                experiment(changed, result, work)

    def test_empty_model_is_rejected(self):
        start, end = MODEL.index('        // device 0'), MODEL.index('    end\n')
        with self.assertRaisesRegex(ValueError, 'empty'):
            self.split(MODEL[:start] + MODEL[end:])


if __name__ == '__main__':
    unittest.main()
