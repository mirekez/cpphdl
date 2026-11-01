#!/usr/bin/env python3
"""Reject a conserved but electrically disconnected resistor-body coupling."""
from pathlib import Path
import sys
import unittest

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))
from extracted_connections import check_capacitor_connections


class Connections(unittest.TestCase):
    def test_resistor_body_coupling(self):
        body = ('.subckt ExtractedAnalog A B GND\n'
                'Xr A B GND rhigh w=0.5u l=53.7u\n'
                'Cwire A GND 1f\n'
                'Cbody body_unused# B 0.02f\n.ends\n')
        with self.assertRaisesRegex(ValueError, '1 capacitor endpoints'):
            check_capacitor_connections(body, ['A', 'B', 'GND'])

    def test_real_series_storage_and_wire_nodes(self):
        text = ('.subckt ExtractedAnalog A B GND\n'
                'Xc1 A middle cap_cmim w=1.35u l=1.36u\n'
                'Xc2 middle wire cap_cmim w=1.35u l=1.36u\n'
                'Rroute wire B 10\n'
                'Cmiddle middle GND 0.3f\n'
                'Cwire wire GND 0.1f\n.ends\n')
        result = check_capacitor_connections(text, ['A', 'B', 'GND'])
        self.assertEqual(result['unattached_endpoints'], 0)


if __name__ == '__main__':
    unittest.main()
