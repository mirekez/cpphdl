#!/usr/bin/env python3
"""Keep the two clock origins distinct when reading routed interface timing."""
import importlib.util
from pathlib import Path
import re
import unittest

path = Path(__file__).resolve().parents[1] / 'receive_interface_timing.py'
spec = importlib.util.spec_from_file_location('interface_report', path)
module = importlib.util.module_from_spec(spec)
spec.loader.exec_module(module)

PATH = '''Startpoint: launch (rising edge-triggered flip-flop clocked by sample_clock)
Endpoint: trial_out[0] (output port clocked by sample_clock)
Path Type: max
Corner: slow
  0.100000000 1.500000000 ^ launch/CLK (sg13g2_dfrbpq_1)
  0.300000000 1.800000000 ^ launch/Q (sg13g2_dfrbpq_1)
  0.400000000 2.200000000 ^ trial_out[0] (out)
                2.200000000 data arrival time
                3.500000000 data required time
                1.300000000 slack (MET)
'''


class InterfaceReport(unittest.TestCase):
    def test_packed_word_and_scalar_arrays_keep_their_public_identity(self):
        netlist = 'output [127:0] trial_out; output [0:15] acquire_out; output [0:15] convert_out;'
        wrapper = '\n'.join('.' + name + '({' + ', '.join(f'{name}[{i}]' for i in range(16)) + '})'
                            for name in ('trial_out', 'acquire_out', 'convert_out'))
        ports = module.interface_ports(netlist, wrapper)
        self.assertEqual(ports['trial_out[127]']['public_port'], 'trial_out[0][7]')
        self.assertFalse(ports['trial_out[24]']['unused_compatibility'])
        self.assertTrue(ports['trial_out[23]']['unused_compatibility'])
        self.assertFalse(ports['acquire_out[12]']['unused_compatibility'])
        self.assertTrue(ports['acquire_out[13]']['unused_compatibility'])
        reversed_netlist = netlist.replace('[0:15]', '[15:0]')
        reversed_wrapper = wrapper.replace('.acquire_out({' + ', '.join(f'acquire_out[{i}]' for i in range(16)),
            '.acquire_out({' + ', '.join(f'acquire_out[{i}]' for i in reversed(range(16))))
        self.assertFalse(module.interface_ports(reversed_netlist, reversed_wrapper)['acquire_out[12]']['unused_compatibility'])
        with self.assertRaises(ValueError):
            module.interface_ports(netlist, wrapper.replace('trial_out[15]', 'trial_out[14]'))
        with self.assertRaises(ValueError):
            module.interface_ports(netlist.replace('[127:0]', '[126:0]'), wrapper)

    def test_local_launch(self):
        result = module.read_path(PATH)
        self.assertAlmostEqual(result['arrival_from_root_clock_ns'], 2.2)
        self.assertAlmostEqual(result['launch_clock_to_boundary_ns'], .7)
        self.assertEqual(result['launch_clock_pin'], 'launch/CLK')

    def test_reported_slew_keeps_clock_origin(self):
        text = re.sub(r'^(  \d)', r'  0.170000000\1', PATH, flags=re.M)
        text = text.replace('Corner: slow\n', 'Corner: slow\n  Slew Delay Time Description\n')
        result = module.read_path(text, 'trial_out[0]')
        self.assertAlmostEqual(result['launch_clock_to_boundary_ns'], .7)
        self.assertAlmostEqual(result['boundary_slew_ns'], .17)
        self.assertEqual(result['boundary_slew_basis'], 'propagated output')
        self.assertEqual(result['boundary_edge'], 'rise')
        with self.assertRaises(ValueError):
            module.read_path(text, 'missing_port')
        with self.assertRaises(ValueError):
            module.read_path(PATH, 'trial_out[0]')

    def test_liberty_slew_is_a_fraction_of_the_edge(self):
        text = 'slew_derate_from_library : 1;\n'
        for edge in ('rise', 'fall'):
            text += f'slew_lower_threshold_pct_{edge} : 20;\n'
            text += f'slew_upper_threshold_pct_{edge} : 80;\n'
            for side in ('input', 'output'):
                text += f'{side}_threshold_pct_{edge} : 50;\n'
        thresholds = module.slew_thresholds(text)
        self.assertEqual(thresholds['slew_upper_threshold_pct_rise'] -
            thresholds['slew_lower_threshold_pct_rise'], 60)
        for bad in (text.replace(': 1;', ': 0.5;'), text.replace(': 80;', ': 10;'),
                    text.replace(': 50;', ': nan;'), text + text):
            with self.assertRaises(ValueError):
                module.slew_thresholds(bad)

    def test_external_control_has_no_local_launch(self):
        text = PATH.replace('  0.100000000 1.500000000 ^ launch/CLK (sg13g2_dfrbpq_1)\n', '')
        self.assertIsNone(module.read_path(text)['launch_clock_to_boundary_ns'])

    def test_return_boundary_uses_routed_delay(self):
        text = PATH.replace('  0.100000000 1.500000000 ^ launch/CLK (sg13g2_dfrbpq_1)',
                            '  0.400000000 0.400000000 ^ input external delay')
        result = module.read_path(text)
        self.assertAlmostEqual(result['input_to_endpoint_ns'], 1.8)
        self.assertAlmostEqual(result['input_boundary_required_ns'], 1.7)

    def test_negative_zero_and_failed_timing(self):
        for value in ('-0.000000000', '-0.1'):
            with self.assertRaises(ValueError):
                module.read_path(PATH.replace('1.300000000 slack', value + ' slack'))
        with self.assertRaises(ValueError):
            module.read_path(PATH.replace('(MET)', '(VIOLATED)'))

    def test_missing_and_multiple_paths(self):
        self.assertIsNone(module.read_path('No paths found.\n'))
        for text in (PATH.replace('Corner: slow\n', ''), PATH + PATH,
                     PATH.replace('data required time', 'unexpected time')):
            with self.assertRaises(ValueError):
                module.read_path(text)


if __name__ == '__main__':
    unittest.main()
