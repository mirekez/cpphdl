#!/usr/bin/env python3
"""Reject missing, double-counted or precharged MOS bias-storage substitutions."""
import argparse
from pathlib import Path
import re
import subprocess
import sys
import tempfile
import unittest

SOURCE = Path(__file__).resolve()
sys.path[:0] = [str(SOURCE.parents[1]), str(SOURCE.parents[2] / 'tests')]
from mos_storage_driver_process import process_deck
from core_transmit_driver import qualified_controls
from mos_geometry import size_mos_instances

parser = argparse.ArgumentParser()
parser.add_argument('--bench', type=Path, required=True)
parser.add_argument('--fixture', type=Path, required=True)
args, remaining = parser.parse_known_args()


class StorageBindings(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.temporary = tempfile.TemporaryDirectory(prefix='cpphdl-mos-storage-')
        cls.root = Path(cls.temporary.name)
        subprocess.run([str(args.bench.resolve()), str(cls.root), 'export',
                        str(qualified_controls(args.fixture.resolve())), '100', '0', '44'], check=True, timeout=60)
        cls.model = (cls.root / 'design.va').read_text()
        cls.bench = (cls.root / 'bench.cir').read_text()

    @classmethod
    def tearDownClass(cls):
        cls.temporary.cleanup()

    def bind(self, bench=None, corner='mos_tt'):
        return process_deck(self.model, self.bench if bench is None else bench,
                            self.root / 'models', corner)

    def test_actual_cpp_export_replaces_only_the_two_storage_stamps(self):
        deck, binding = self.bind()
        self.assertEqual(len(re.findall(r'^Cdevice', deck, re.M)), 613)
        self.assertEqual(len(re.findall(r'^XbiasStorage ', deck, re.M)), 1)
        self.assertEqual(len(re.findall(r'^XcascodeStorage ', deck, re.M)), 1)
        self.assertNotEqual(binding['bias_storage']['node'], binding['cascode_storage']['node'])
        self.assertEqual(binding['bias_storage']['copies'], 40)
        self.assertEqual(binding['cascode_storage']['copies'], 220)
        folded, inventory = size_mos_instances(deck)
        self.assertEqual(len(inventory), 305)  # 304 original MOS + the repeated HV storage model.
        self.assertNotIn('cpphdl_channel', folded)

    def test_nonzero_initial_charge_cannot_be_silently_discarded(self):
        _, binding = self.bind()
        for value, node in ((400e-12, binding['bias_storage']['node']),
                            (50e-12, binding['cascode_storage']['node'])):
            with self.subTest(storage=value):
                line = next(line for line in self.bench.splitlines() if line.startswith('Cdevice')
                            and line.split()[1:3] == [node, '0']
                            and abs(float(line.split()[3]) - value) < 1e-23)
                for initial in ('0.25', '0.025', '1e-9'):
                    with self.subTest(initial=initial):
                        changed = re.sub(r' IC=\S+$', ' IC=' + initial, line)
                        self.assertNotEqual(changed, line)
                        with self.assertRaisesRegex(ValueError, 'cold-start'):
                            self.bind(self.bench.replace(line, changed, 1))

    def test_missing_original_storage_is_rejected(self):
        line = next(line for line in self.bench.splitlines() if line.startswith('Cdevice')
                    and abs(float(line.split()[3]) - 400e-12) < 1e-23)
        with self.assertRaises(ValueError):
            self.bind(self.bench.replace(line + '\n', '', 1))

    def test_transistor_corners_do_not_invent_moscap_corners(self):
        for corner in ('mos_tt', 'mos_ss', 'mos_ff'):
            with self.subTest(corner=corner):
                deck, binding = self.bind(corner=corner)
                self.assertIn(f'cornerMOShv.lib" {corner}', deck)
                self.assertIn('cornerMOSCAP.lib" moscap_tt', deck)
                self.assertEqual(binding['bias_storage']['process_corner'], 'moscap_tt')
                self.assertEqual(binding['cascode_storage']['corner'], corner)


if __name__ == '__main__':
    unittest.main(argv=[sys.argv[0]] + remaining)
