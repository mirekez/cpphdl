#!/usr/bin/env python3
"""Reject changed current paths, bias networks and charge before PSP binding."""
import argparse
from pathlib import Path
import re
import subprocess
import sys
import tempfile
import unittest

SOURCE = Path(__file__).resolve()
sys.path[:0] = [str(SOURCE.parents[1]), str(SOURCE.parents[2] / 'tests')]
from cascoded_driver_process import process_deck
from core_transmit_driver import qualified_controls
from mos_geometry import size_mos_instances

parser = argparse.ArgumentParser()
parser.add_argument('--bench', type=Path, required=True)
parser.add_argument('--fixture', type=Path, required=True)
args, remaining = parser.parse_known_args()


class BindingTest(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.temporary = tempfile.TemporaryDirectory(prefix='cpphdl-cascoded-binding-')
        cls.work = Path(cls.temporary.name)
        controls = qualified_controls(args.fixture.resolve())
        subprocess.run([str(args.bench.resolve()), str(cls.work), 'export', str(controls), '100', '0', '44'],
                       check=True, timeout=30)
        cls.model = (cls.work / 'design.va').read_text()
        cls.original = (cls.work / 'bench.cir').read_text()

    @classmethod
    def tearDownClass(cls):
        cls.temporary.cleanup()

    def bind(self, model=None, deck=None):
        return process_deck(self.model if model is None else model, self.original if deck is None else deck,
                            self.work / 'models', 'mos_tt')

    def test_actual_cpp_circuit_preserves_every_device(self):
        deck, bindings = self.bind()
        self.assertEqual(sum(bindings['roles'].values()), 304)
        self.assertEqual(sum(d['kind'] == 'R' for d in bindings['devices']), 39)
        self.assertEqual(sum(line.startswith('Cdevice') for line in deck.splitlines()), 615)
        self.assertNotIn('cpphdl_channel', deck)
        folded, geometry = size_mos_instances(deck)
        self.assertEqual(len(re.findall(r'^Xphysical', folded, re.M)), 304)
        for role, width in [('main_current_cascode', 1500), ('history_current_cascode', 500),
                            ('main_steering', 400), ('history_steering', 200)]:
            self.assertTrue(all(d['width_um'] == width for d in bindings['devices'] if d.get('role') == role))

    def test_cascode_cannot_bypass_the_current_setting_drain(self):
        _, bindings = self.bind()
        device = next(d for d in bindings['devices'] if d.get('role') == 'main_current_cascode')
        marker = f'cpphdl_channel(V({device["gate"]}, {device["b"]}), V({device["a"]}, {device["b"]})'
        line = next(line for line in self.model.splitlines() if marker in line)
        changed = self.model.replace(line, line.replace(device['b'], 'n999999'), 1)
        with self.assertRaisesRegex(ValueError, 'cascode'):
            self.bind(model=changed)

    def test_cascode_bias_divider_cannot_change(self):
        self.assertIn(' / 3600.0;', self.model)
        with self.assertRaisesRegex(ValueError, 'divider'):
            self.bind(model=self.model.replace(' / 3600.0;', ' / 3601.0;', 1))

    def test_bias_filter_cannot_be_reduced(self):
        _, bindings = self.bind()
        bias = next(d['gate'] for d in bindings['devices'] if d.get('role') == 'main_sink')
        line = next(line for line in self.original.splitlines() if line.startswith('Cdevice')
                    and set(line.split()[1:3]) == {bias, '0'} and abs(float(line.split()[3]) - 400e-12) < 1e-23)
        fields = line.split()
        fields[3] = '200e-12'
        with self.assertRaisesRegex(ValueError, 'storage'):
            self.bind(deck=self.original.replace(line, ' '.join(fields)))

    def test_unbound_laws_and_missing_charge_rejected(self):
        changed = self.model.replace('    end\nendmodule\n', '        I(n2, n0) <+ 1.0;\n    end\nendmodule\n')
        with self.assertRaisesRegex(ValueError, 'unbound'):
            self.bind(model=changed)
        line = next(line for line in self.original.splitlines() if line.startswith('Cdevice'))
        with self.assertRaisesRegex(ValueError, 'charge'):
            self.bind(deck=self.original.replace(line + '\n', '', 1))


if __name__ == '__main__':
    unittest.main(argv=[sys.argv[0]] + remaining)
