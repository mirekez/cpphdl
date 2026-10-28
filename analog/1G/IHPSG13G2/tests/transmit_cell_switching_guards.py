#!/usr/bin/env python3
"""Reject shared wrong behavior, missing steering and incomplete trajectories."""
import unittest

from transmit_cell_switching import measure


def trajectory():
    rows = []
    for sample in range(2101):
        time = sample * 20e-12
        positive = int((time - 2e-9) // 8e-9) % 2 == 0 if time >= 2e-9 else False
        p, n = (.0075, 0.) if positive else (0., .0075)
        rows.append([time, p, n, p, n])
    return rows


class SwitchingGuards(unittest.TestCase):
    def test_complete_alternating_trajectory(self):
        result = measure(trajectory(), .0075)
        self.assertEqual(result['observed_symbols'], 5)
        self.assertEqual(result['maximum_relative_difference'], 0)

    def test_shared_wrong_amplitude_is_not_a_passing_comparison(self):
        rows = trajectory()
        for row in rows:
            row[1:] = [value * .8 for value in row[1:]]
        with self.assertRaisesRegex(ValueError, 'independent plateau'):
            measure(rows, .0075)

    def test_reference_and_physical_checked_separately(self):
        for offset, flow in ((1, 'extracted'), (3, 'reference')):
            rows = trajectory()
            for row in rows:
                row[offset], row[offset + 1] = 0., .0075
            with self.subTest(flow=flow), self.assertRaisesRegex(ValueError, flow + ':'):
                measure(rows, .0075)

    def test_common_extra_current_fails_even_with_correct_difference(self):
        rows = trajectory()
        for row in rows:
            row[1] += .001
            row[2] += .001
        with self.assertRaisesRegex(ValueError, 'extracted:'):
            measure(rows, .0075)

    def test_missing_time_and_nonfinite_data_rejected(self):
        for rows in (trajectory()[:-1], trajectory()[::2],
                     [[*row[:1], float('nan'), *row[2:]] for row in trajectory()]):
            with self.assertRaisesRegex(ValueError, 'trajectory'):
                measure(rows, .0075)


if __name__ == '__main__':
    unittest.main()
