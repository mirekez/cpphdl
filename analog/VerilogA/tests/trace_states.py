#!/usr/bin/env python3
"""Regression tests for retaining all electrical states at repeated times."""
import gzip
from pathlib import Path
import sys
import tempfile
import unittest

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))
from trace_codec import normalize_roundoff_duplicates
from trace_states import StateEnvelope, read_states


class StatesTest(unittest.TestCase):
    def test_equal_time_jump_is_retained(self):
        rows = [[0., 0.], [1., 1.], [1., 3.], [2., 4.]]
        envelope = StateEnvelope(rows)
        self.assertEqual(envelope.bounds(1., 1), (1., 3.))
        self.assertEqual(envelope.bounds(.5, 1), (.5, 1.5))
        self.assertEqual(envelope.bounds(1.5, 1), (2.5, 3.5))
        self.assertEqual(envelope.bounds(0., 1), (0., 0.))
        self.assertEqual(envelope.bounds(2., 1), (4., 4.))

    def test_correlated_pin_difference(self):
        envelope = StateEnvelope([[0., 1., 2., -1.], [1., 4., 6., -2.], [1., 8., 10., -2.]])
        self.assertEqual(envelope.projected_bounds(.5, lambda r: r[1] - r[2] - r[3]), (0., 0.))

    def test_range_and_order_errors(self):
        with self.assertRaisesRegex(ValueError, 'backward'):
            StateEnvelope([[1., 0.], [0., 0.]])
        envelope = StateEnvelope([[0., 0.], [1., 1.]])
        for time in [-1., 2., float('nan')]:
            with self.assertRaisesRegex(ValueError, 'outside'):
                envelope.bounds(time, 1)

    def test_reader_rejects_bad_states(self):
        with tempfile.TemporaryDirectory() as directory:
            path = Path(directory) / 'states.dat'
            for bad in ['4 1\n', '11 nan\n', '11 0 0\n']:
                path.write_text('time V\n' + ''.join(f'{i} 0\n' for i in range(10)) + bad)
                with self.assertRaises(ValueError):
                    read_states(path, ['time_s', 'V'])

    def test_gzip_audit_restores_every_original_state(self):
        with tempfile.TemporaryDirectory() as directory:
            path = Path(directory) / 'states.dat.gz'
            text = 'time V\n' + ''.join(f'{i} 1\n' + (f'{i} 1.000000000000001\n' if i == 5 else '')
                                            for i in range(10))
            with gzip.open(path, 'wt') as file:
                file.write(text)
            before = read_states(path, ['time_s', 'V'])
            audit = normalize_roundoff_duplicates(path)
            after = read_states(path, ['time_s', 'V'], audit=audit)
            self.assertEqual(before, after)
            self.assertEqual(len(after), 11)
            self.assertEqual(StateEnvelope(after).bounds(5., 1), (1., 1.000000000000001))


if __name__ == '__main__':
    unittest.main()
