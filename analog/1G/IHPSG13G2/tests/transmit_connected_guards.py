#!/usr/bin/env python3
"""Fault checks for complete TX model composition and the loaded experiment."""
import argparse
import copy
import json
from pathlib import Path
import subprocess
import sys
import tempfile
import unittest

SOURCE = Path(__file__).resolve()
ROOT = SOURCE.parents[3]
sys.path[:0] = [str(SOURCE.parents[1]), str(ROOT / '1G/tests'), str(SOURCE.parent)]
from transmit_physical_assembly import INTERFACES, assemble, namespace_model
from transmit_connected_deck import experiment, compare
from core_transmit_driver import qualified_controls, HEADER, TRIM_HEADER
from transmit_driver import controls
from transmit_connected_startup import compare as compare_startup

parser = argparse.ArgumentParser()
parser.add_argument('--exporter', type=Path, required=True)
parser.add_argument('--fixture', type=Path, required=True)
args, remaining = parser.parse_known_args()


class ConnectedTransmitter(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.temporary = tempfile.TemporaryDirectory(prefix='cpphdl-connected-tx-')
        cls.export = Path(cls.temporary.name) / 'export'
        subprocess.run([args.exporter.resolve(), cls.export], check=True, timeout=30)
        cls.words = controls(qualified_controls(args.fixture.resolve()))
        cls.models = {kind: ('.subckt Unit ' + ' '.join(ports) +
            '\nRinternal ' + ports[0] + ' ' + ports[-1] + ' 1\n.ends Unit\n', 'Unit', ports)
            for kind, ports in INTERFACES.items()}
        cls.model, cls.assembly = assemble(cls.export, cls.models)

    @classmethod
    def tearDownClass(cls):
        cls.temporary.cleanup()

    def test_storage_is_connected_to_actual_current_cell_anchors(self):
        instances = {part['name']: part for part in self.assembly['instances']}
        for bank in ('main', 'history'):
            for bit in range(4):
                cell = bank + '_' + str(bit)
                self.assertEqual(instances[cell]['ports'], instances[cell + '_storage']['ports'])
        self.assertEqual(sorted(e for p in instances.values() for e in p['elements']), list(range(931)))
        manifest = json.loads((self.export / 'partition.json').read_text())
        aliases = {value: name for name, value in manifest['ports'].items()}
        for part in manifest['blocks'][:-1]:
            self.assertEqual(instances[part['name']]['ports'],
                {p: aliases.get(n, 'node_' + str(n)) for p, n in part['ports'].items()})

    def test_missing_storage_model_rejected(self):
        models = dict(self.models)
        del models['bias_storage']
        with self.assertRaisesRegex(ValueError, 'incomplete'):
            assemble(self.export, models)

    def test_wrong_extracted_port_order_rejected(self):
        models = dict(self.models)
        text, name, ports = models['main']
        models['main'] = text, name, list(reversed(ports))
        with self.assertRaisesRegex(ValueError, 'port order'):
            assemble(self.export, models)

    def test_extracted_substrate_port_cannot_be_omitted(self):
        models = dict(self.models)
        text, name, ports = models['output_storage']
        models['output_storage'] = text, name, [p for p in ports if p != 'VSS']
        with self.assertRaisesRegex(ValueError, 'interface'):
            assemble(self.export, models)

    def test_namespace_preserves_process_models_and_rc_values(self):
        text = ('.subckt Unit A B\nXfinger A A B B sg13_hv_nmos w=5u l=.5u\n'
                'Xlocal A B Child\nRwire A B 2.5\n.ends Unit\n'
                '.subckt Child A B\nCwire A B 2e-15\n.ends Child\n')
        result, name = namespace_model(text, 'Unit', 'TX', ['A', 'B'])
        self.assertEqual(name, 'TX_Unit')
        self.assertEqual(result, text.replace('Unit', 'TX_Unit').replace('Child', 'TX_Child'))

    def test_namespace_rejects_duplicate_subcircuit(self):
        with self.assertRaisesRegex(ValueError, 'port order'):
            namespace_model('.subckt Unit A B\n.ends\n.subckt unit A B\n.ends\n',
                            'Unit', 'TX', ['A', 'B'])

    def test_loaded_experiment_retains_all_control_and_voltage_observations(self):
        deck, interface, header = experiment(self.model, self.assembly, self.words, Path('/models'))
        self.assertEqual(header, HEADER + TRIM_HEADER + ['CASCODE_BIAS'] +
                         ['LV_EXPOSURE' + str(i) for i in range(22)])
        self.assertEqual(sum(line.startswith('Vcmd_') for line in deck.splitlines()), 16)
        self.assertEqual(sum(line.startswith('Vtrim') for line in deck.splitlines()), 6)
        self.assertIn('RLOAD P N 100\n', deck)
        self.assertIn('RSUP SOURCE VDD .25\n', deck)
        self.assertIn('RCORE CORE_SOURCE VLOGIC 2\n', deck)
        self.assertIn('let observation18 = v(P,N)\n', deck)
        self.assertEqual(interface['warm_words'], 34)
        self.assertAlmostEqual(interface['stop_s'], 512e-9 + len(self.words) * 8e-9)

    def test_cold_mim_state_is_explicit_without_duplicate_parameters(self):
        model = self.model + '\nXcap A B cap_cmim w=7u l=7u ic=0\n'
        deck, _, _ = experiment(model, self.assembly, self.words, Path('/models'))
        self.assertIn('Xcap A B cap_cmim w=7u l=7u ic=0\n', deck)
        self.assertNotIn('ic=0 ic=0', deck)
        with self.assertRaisesRegex(ValueError, 'initial MIM'):
            experiment(model.replace('ic=0', 'ic=1'), self.assembly, self.words, Path('/models'))

    def test_changed_external_ports_rejected(self):
        assembly = copy.deepcopy(self.assembly)
        assembly['ports'][3:5] = ['N', 'P']
        with self.assertRaisesRegex(ValueError, 'interface'):
            experiment(self.model, assembly, self.words, Path('/models'))

    def test_comparison_checks_both_retained_state_bounds(self):
        first = {'plateau_bounds_V': [[[0., 0.] for _ in range(6)]]}
        second = copy.deepcopy(first)
        self.assertEqual(compare(first, second)['maximum_plateau_difference_V'], 0)
        second['plateau_bounds_V'][0][2][1] = .010001
        with self.assertRaisesRegex(ValueError, 'departure'):
            compare(first, second)
        second['plateau_bounds_V'][0][2] = [0.]
        with self.assertRaisesRegex(ValueError, 'incomplete'):
            compare(first, second)

    def test_numerical_refinement_checks_every_voltage_column(self):
        nominal = [[0., 0., 1.], [1., .5, 0.], [2., 1., 1.]]
        refined = [[0., 0., 1.], [.5, .25, .5], [1., .5, 0.], [1.5, .75, .5], [2., 1., 1.]]
        self.assertEqual(compare_startup(nominal, refined)['maximum_voltage_difference_V'], 0)
        refined[-1][-1] += .00101
        with self.assertRaisesRegex(ValueError, '1 mV'):
            compare_startup(nominal, refined)

    def test_numerical_refinement_rejects_missing_startup(self):
        with self.assertRaisesRegex(ValueError, 'omits startup'):
            compare_startup([[0., 0.], [1., 1.]], [[.1, .1], [1., 1.]])


if __name__ == '__main__':
    unittest.main(argv=[sys.argv[0]] + remaining)
