#!/usr/bin/env python3
"""Check body coupling attachment and deliberate intrinsic-charge partition."""
from pathlib import Path
import sys
import unittest

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))
from resistor_body_extraction import adapt
from extract_analog import inventory


REFERENCE = '''.subckt ExtractedAnalog A B GND GATE
Xres A B GND rppd w=0.5u l=20u
C_a A GND 3f
C_b B GND 3f
C_couple body# GATE 0.2f
.ends
'''
EXT = '''scale 1000 1 0.5
node "body#" 100 4000 0 0 pres
device rsubckt rppd 0 0 1 1 w=100 l=4000 "GND" "body#" 0 0 "A" 100 0,0 "B" 100 0,0
'''


class BodyExtraction(unittest.TestCase):
    def test_contact_and_coupling_conservation(self):
        distributed = REFERENCE.replace('Xres A B', 'Xres a1 B').replace(
            'C_a A GND 3f', 'Rwire A a1 5\nC_a A GND 1.5f\nC_aa a1 GND 1.5f')
        text, audit = adapt(REFERENCE, distributed, EXT, ['A', 'B', 'GND', 'GATE'], 'GND')
        net = inventory(text, ['A', 'B', 'GND', 'GATE'])
        self.assertEqual(net['models'], {'rppd': 2})
        self.assertAlmostEqual(sum(d['length_m'] for d in net['geometry']), 20e-6)
        self.assertAlmostEqual(audit['removed_duplicate_body_substrate_F'] / 1e-15, 4.)
        coupling = inventory(REFERENCE, ['A', 'B', 'GND', 'GATE'])['capacitors'][-1]
        self.assertIn(coupling, net['capacitors'])
        self.assertEqual(audit['connections']['unattached_endpoints'], 0)
        self.assertEqual(net['resistors'], [('A', 'a1', 5.)])

    def test_missing_body_or_charge_is_rejected(self):
        for ext in (EXT.replace('4000 0 0', '9000 0 0'), EXT.replace('"body#" 100', '"wrong#" 100')):
            with self.assertRaises(ValueError):
                adapt(REFERENCE, REFERENCE, ext, ['A', 'B', 'GND', 'GATE'], 'GND')

    def test_only_half_output_quantum_can_be_rounding(self):
        ext = EXT.replace('100 4000 0 0', '100 6000 0 0')
        rounded = REFERENCE.replace('GND 3f', 'GND 2.999998f')
        text, audit = adapt(rounded, rounded, ext, ['A', 'B', 'GND', 'GATE'], 'GND')
        self.assertAlmostEqual(sum(audit['output_rounding_F'].values()) / 1e-21, 4., places=6)
        self.assertAlmostEqual(audit['removed_duplicate_body_substrate_F'] / 1e-15, 5.999996)
        self.assertEqual(audit['connections']['unattached_endpoints'], 0)
        boundary = REFERENCE.replace('GND 3f', 'GND 2.999995f')
        _, at_half = adapt(boundary, boundary, ext, ['A', 'B', 'GND', 'GATE'], 'GND')
        self.assertAlmostEqual(sum(at_half['output_rounding_F'].values()) / 1e-21, 10., places=6)
        broken = REFERENCE.replace('GND 3f', 'GND 2.999994f')
        with self.assertRaisesRegex(ValueError, 'body capacitance exceeds'):
            adapt(broken, broken, ext, ['A', 'B', 'GND', 'GATE'], 'GND')


if __name__ == '__main__':
    unittest.main()
