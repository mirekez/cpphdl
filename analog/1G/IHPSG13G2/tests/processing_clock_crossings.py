#!/usr/bin/env python3
"""Reject incomplete or out-of-budget extracted Gray-bus reports."""
import copy
from pathlib import Path
import sys
import unittest

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))
from processing_clock_crossings import BUSES, qualify


class Crossings(unittest.TestCase):
    def setUp(self):
        self.paths = {}
        for bus, (_, _, width) in BUSES.items():
            for bit in range(width):
                for corner in ('slow', 'fast'):
                    for delay in ('min', 'max'):
                        self.paths[bus, bit, corner, delay] = {'checks': [dict(
                            type='path_delay', startpoint=f'launch_{bit}/Q', endpoint=f'capture_{bit}/D',
                            source_clock='acquisition' if bus == 'count' else 'reference',
                            target_clock='reference' if bus == 'count' else 'acquisition',
                            source_clock_edge='rise', source_clock_path=[dict(arrival=.3e-9)],
                            source_path=[dict(pin=f'launch_{bit}/Q')],
                            data_arrival_time=.6e-9, slack=.1e-9)]}

    def test_complete_matrix(self):
        result = qualify(self.paths)
        self.assertEqual(result['count']['bits'], 14)
        self.assertEqual(result['reference']['bits'], 8)
        self.assertAlmostEqual(result['count']['conservative_arrival_window_ns'], .6)

    def test_missing_bit_corner_or_transition_report(self):
        del self.paths['count', 13, 'fast', 'min']
        with self.assertRaisesRegex(ValueError, 'incomplete'):
            qualify(self.paths)

    def test_empty_path_from_accidental_clock_cut(self):
        self.paths['count', 0, 'slow', 'max'] = {'checks': []}
        with self.assertRaisesRegex(ValueError, 'missing or ambiguous'):
            qualify(self.paths)

    def test_common_corner_latency_is_not_bit_skew(self):
        for key, value in self.paths.items():
            value['checks'][0]['source_clock_path'][0]['arrival'] = .6e-9 if key[2] == 'slow' else .2e-9
        result = qualify(self.paths)
        self.assertEqual(result['count']['corners']['slow']['source_clock_spread_ns'], 0)
        self.assertEqual(result['count']['corners']['fast']['source_clock_spread_ns'], 0)

    def test_delay_setup_and_clock_spread_fail_independently(self):
        original = copy.deepcopy(self.paths)
        for change, message in (
                ({'data_arrival_time':1.01e-9}, 'data delay'),
                ({'slack':-.01e-9}, 'maximum delay/setup'),
                ({'source_clock_path':[dict(arrival=.56e-9)]}, 'source-clock spread'),
                ({'data_arrival_time':float('nan')}, 'nonfinite'),
                ({'target_clock':'acquisition'}, 'endpoints')):
            self.paths = copy.deepcopy(original)
            self.paths['count', 0, 'slow', 'max']['checks'][0].update(change)
            with self.subTest(message=message), self.assertRaisesRegex(ValueError, message):
                qualify(self.paths)


if __name__ == '__main__':
    unittest.main()
