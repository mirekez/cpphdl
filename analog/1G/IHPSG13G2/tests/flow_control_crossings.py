#!/usr/bin/env python3
"""Reject missing or unsafe timing evidence for the frame SRAM Gray pointers."""
import copy
from pathlib import Path
import sys
import unittest

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))
from flow_control_crossings import BUSES, qualify, register_bindings


class Crossings(unittest.TestCase):
    def setUp(self):
        self.paths = {}
        self.module = dict(netnames={}, cells={})
        serial = 0
        for bus, spec in BUSES.items():
            for state in ('source', 'target'):
                bits = []
                for bit in range(spec['width']):
                    serial += 1
                    bits.append(serial)
                    self.module['cells'][f'_{serial}_'] = dict(type='sg13g2_dfrbpq_1',
                                                             connections={'Q': [serial]})
                self.module['netnames'][spec[state]] = dict(bits=bits)
            for bit in range(spec['width']):
                for corner in ('slow', 'fast'):
                    for delay in ('min', 'max'):
                        self.paths[bus, bit, corner, delay] = {'checks': [dict(
                            type='path_delay', path_type=delay, startpoint=f'launch{bit}/Q', endpoint=f'sink{bit}/D',
                            source_clock=spec['source_clock'], target_clock=spec['target_clock'],
                            source_clock_edge='rise', target_clock_edge='rise', source_clock_path=[dict(arrival=.3e-9)],
                            data_arrival_time=1.2e-9, slack=.5e-9)]}
        self.netlist = {'modules': {'FlowControlledRepeater': self.module}}

    def test_complete_matrix(self):
        result = qualify(self.paths)
        self.assertEqual(sum(v['bits'] for v in result.values()), 100)
        self.assertEqual(len(register_bindings(self.netlist)), 200)

    def test_missing_bit_or_clock_cut(self):
        key = 'a_to_b_write', 12, 'fast', 'min'
        original = copy.deepcopy(self.paths)
        del self.paths[key]
        with self.assertRaisesRegex(ValueError, 'incomplete'):
            qualify(self.paths)
        original[key] = {'checks': []}
        with self.assertRaisesRegex(ValueError, 'missing or ambiguous'):
            qualify(original)

    def test_delay_clock_spread_and_wrong_domain(self):
        key = 'b_to_a_read', 2, 'slow', 'max'
        for change, error in [({'data_arrival_time': 2.01e-9}, 'data delay'),
                              ({'slack': -.01e-9}, 'reserve'),
                              ({'source_clock_path': [{'arrival': .81e-9}]}, 'spread'),
                              ({'source_clock': 'rx_clk'}, 'endpoints'),
                              ({'target_clock_edge': 'fall'}, 'endpoints'),
                              ({'path_type': 'min'}, 'endpoints'),
                              ({'data_arrival_time': float('nan')}, 'nonfinite')]:
            paths = copy.deepcopy(self.paths)
            paths[key]['checks'][0].update(change)
            with self.subTest(error=error), self.assertRaisesRegex(ValueError, error):
                qualify(paths)

    def test_corner_latency_is_not_interbit_skew(self):
        for (_, _, corner, _), value in self.paths.items():
            value['checks'][0]['source_clock_path'][0]['arrival'] = 1e-9 if corner == 'slow' else .2e-9
        self.assertEqual(qualify(self.paths)['a_to_b_write']['corners']['slow']['source_clock_spread_ns'], 0)

    def test_missing_state_or_combinational_driver(self):
        source = BUSES['a_to_b_write']['source']
        del self.module['netnames'][source]
        with self.assertRaisesRegex(ValueError, 'missing semantic'):
            register_bindings(self.netlist)
        self.setUp()
        self.module['cells']['_1_']['type'] = 'sg13g2_buf_1'
        with self.assertRaisesRegex(ValueError, 'state register'):
            register_bindings(self.netlist)

    def test_shorted_state_drivers(self):
        self.module['cells']['duplicate'] = copy.deepcopy(self.module['cells']['_1_'])
        with self.assertRaisesRegex(ValueError, 'ambiguous'):
            register_bindings(self.netlist)

    def test_payload_request_relative_clock_skew(self):
        # Every individual bus has zero skew, but its request launches late.
        for (bus, _, corner, _), value in self.paths.items():
            if bus == 'a_request' and corner == 'slow':
                value['checks'][0]['source_clock_path'][0]['arrival'] = .81e-9
        with self.assertRaisesRegex(ValueError, 'payload/request'):
            qualify(self.paths)


if __name__ == '__main__':
    unittest.main()
