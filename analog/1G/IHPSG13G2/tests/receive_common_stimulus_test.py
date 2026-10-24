#!/usr/bin/env python3
"""Reject wrong line-source schedules even if their extrema remain correct."""
import math
import unittest

from receive_common_stimulus import source_profile


class SourceProfile(unittest.TestCase):
    def fixture(self):
        # Hand-calculated source levels at the first two symbols. Symbol31
        # has the same values as symbol1; check extension to the final time.
        zero = [(2.3, 2.3)] * 8
        first = [(1.6, 2.6), (1.85, 2.35), (2.1, 2.1), (2.35, 1.85),
                 (2.6, 1.6), (1.6, 2.6), (1.85, 2.35), (2.1, 2.1)]
        second = [(2.55, 2.05), (2.8, 1.8), (1.8, 2.8), (2.05, 2.55),
                  (2.3, 2.3), (2.55, 2.05), (2.8, 1.8), (1.8, 2.8)]
        midpoint = lambda a, b: [tuple((x + y) / 2 for x, y in zip(p, q)) for p, q in zip(a, b)]
        states = [(0., zero), (.05, midpoint(zero, first)), (.1, first), (6., first),
                  (8.05, midpoint(first, second)), (8.1, second), (256., second)]
        rows = []
        for ns, levels in states:
            row = [16e-6 + ns * 1e-9, 1.8, 3.3, 1.8, 1.3, 1., 2., 1.]
            for lane, (p, n) in enumerate(levels):
                plus, minus = 1.82 + lane * .01, 1.94 - lane * .01
                row += [(500 * p + 25 * plus) / 525,
                        (500 * n + 25 * minus) / 525, plus, minus]
            rows.append(row)
        return rows

    def test_finite_impedance_sources_and_ramp_interpolation(self):
        result = source_profile(self.fixture())
        self.assertTrue(result['checks']['source_profile'])
        self.assertLess(result['maximum_source_error_V'], 1e-10)
        self.assertEqual(result['checked_terminal_states'], 7 * 16)

    def test_one_wrong_terminal_in_last_lane(self):
        rows = self.fixture()
        rows[4][36] += .001
        self.assertFalse(source_profile(rows)['checks']['source_profile'])

    def test_slowed_edge_cannot_hide_behind_correct_extrema(self):
        rows = self.fixture()
        rows[1][8] = (rows[0][8] * 3 + rows[2][8]) / 4
        self.assertFalse(source_profile(rows)['checks']['source_profile'])

    def test_swapped_legs_and_omitted_source_drop(self):
        rows = self.fixture()
        rows[5][36], rows[5][37] = rows[5][37], rows[5][36]
        self.assertFalse(source_profile(rows)['checks']['source_profile'])
        rows = self.fixture()
        rows[2][8] = 1.6
        self.assertFalse(source_profile(rows)['checks']['source_profile'])

    def test_malformed_evidence(self):
        rows = self.fixture()
        rows[0][8] = math.nan
        for malformed in ([], rows, [self.fixture()[0][:-1]]):
            with self.assertRaises(ValueError):
                source_profile(malformed)


if __name__ == '__main__':
    unittest.main()
