"""Negative controls for the sampler's independent and comparison contracts."""
from pathlib import Path
import sys
import unittest

sys.path.insert(0, str(Path(__file__).resolve().parent))
from receive_sampler_extracted import measure


class SamplerChecks(unittest.TestCase):
    def rows(self):
        rows = []
        for step in range(30001):
            time = step * 2e-12
            source = 1.6 if time < 48e-9 else 1.85
            clock = 3.3 if time < 40e-9 else 0
            rows.append([time, 1.6, 1.6, source, source, clock, clock, 3.3, 3.3])
        return rows

    def test_complete_contract(self):
        self.assertEqual(measure(self.rows(), 1.6)['status'], 'pass')

    def test_output_tracking_after_close_rejected(self):
        rows = self.rows()
        for row in rows:
            row[1] = row[3]
        result = measure(rows, 1.6)
        self.assertEqual(result['physical']['status'], 'fail')
        self.assertEqual(result['schematic']['status'], 'pass')

    def test_comparison_cannot_mask_common_failure(self):
        rows = self.rows()
        for row in rows:
            row[1] = row[2] = 1.65
        result = measure(rows, 1.6)
        self.assertEqual(result['comparison']['status'], 'pass')
        self.assertEqual(result['physical']['status'], 'fail')
        self.assertEqual(result['schematic']['status'], 'fail')
        self.assertEqual(result['status'], 'fail')

    def test_comparison_has_separate_budget(self):
        rows = self.rows()
        for row in rows:
            row[1] += .006
        result = measure(rows, 1.6)
        self.assertEqual(result['physical']['status'], 'pass')
        self.assertEqual(result['schematic']['status'], 'pass')
        self.assertEqual(result['comparison']['status'], 'fail')

    def test_missing_disturbance_rejected(self):
        rows = self.rows()
        for row in rows:
            row[3] = row[4] = 1.6
        result = measure(rows, 1.6)
        self.assertFalse(result['physical']['checks']['held_input_disturbance'])
        self.assertFalse(result['schematic']['checks']['held_input_disturbance'])

    def test_truncated_or_coarse_trace_rejected(self):
        for rows in (self.rows()[:-1], self.rows()[::2]):
            with self.assertRaises(ValueError):
                measure(rows, 1.6)


if __name__ == '__main__':
    unittest.main()
