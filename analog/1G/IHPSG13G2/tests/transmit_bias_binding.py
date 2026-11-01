#!/usr/bin/env python3
"""Reject changes between actual C++ trim branches and qualified bias geometry."""
import argparse
import copy
import json
from pathlib import Path
import subprocess
import sys
import tempfile
import unittest

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))
from extract_analog import digest
from transmit_bias_binding import bind_current_bias

parser = argparse.ArgumentParser()
parser.add_argument('--exporter', type=Path, required=True)
parser.add_argument('--proof', type=Path, required=True)
args, remaining = parser.parse_known_args()


class BiasBinding(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.proof = json.loads(args.proof.read_text())
        if (cls.proof.get('status') != 'pass' or cls.proof.get('local_drc_violations') != 0 or
                cls.proof.get('missing_switch_rejected') is not True):
            raise ValueError('passing bias geometry/DC and missing-switch proof required')
        for group in ('source_sha256', 'artifact_sha256'):
            for path, value in cls.proof[group].items():
                if digest(path) != value:
                    raise ValueError('changed bias fixture: ' + path)
        cls.temporary = tempfile.TemporaryDirectory(prefix='cpphdl-tx-bias-binding-')
        cls.export = Path(cls.temporary.name) / 'export'
        subprocess.run([str(args.exporter.resolve()), str(cls.export)], check=True, timeout=30)

    @classmethod
    def tearDownClass(cls):
        cls.temporary.cleanup()

    def setUp(self):
        self.geometry = copy.deepcopy(self.proof['geometry'])

    def check(self):
        return bind_current_bias(self.export / 'current_bias.va', self.geometry)

    def part(self, name):
        return next(p for p in self.geometry['components'] if p['name'] == name)

    def test_actual_export_matches_and_placement_order_is_irrelevant(self):
        self.assertEqual(self.check()['trim_branches'], 6)
        self.geometry['components'].reverse()
        self.assertEqual(self.check()['devices'], 16)

    def test_swapped_physical_trim_bits_rejected(self):
        a, b = self.part('BiasSwitch0'), self.part('BiasSwitch1')
        a['connections']['G'], b['connections']['G'] = b['connections']['G'], a['connections']['G']
        with self.assertRaisesRegex(ValueError, 'differ from physical'):
            self.check()

    def test_missing_bias_device_rejected(self):
        self.geometry['components'].remove(self.part('BiasReference'))
        with self.assertRaisesRegex(ValueError, 'differ from physical'):
            self.check()

    def test_changed_resistor_weight_rejected(self):
        self.part('BiasTrim3')['geometry']['requested_resistance_Ohm'] *= 2
        with self.assertRaisesRegex(ValueError, 'differ from physical'):
            self.check()

    def test_changed_channel_dimension_rejected(self):
        self.part('BiasReference')['geometry']['total_width_um'] = 90
        with self.assertRaisesRegex(ValueError, 'differ from physical'):
            self.check()

    def test_wrong_body_connection_rejected(self):
        self.part('BiasTrim0')['connections']['SUB'] = 'BIAS'
        with self.assertRaisesRegex(ValueError, 'resistor body'):
            self.check()

    def test_missing_bias_port_rejected(self):
        del self.geometry['terminals']['TRIM_N5']
        with self.assertRaisesRegex(ValueError, 'differ from physical'):
            self.check()


if __name__ == '__main__':
    unittest.main(argv=[sys.argv[0]] + remaining)
