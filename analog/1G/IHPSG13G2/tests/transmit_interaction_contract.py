#!/usr/bin/env python3
"""Check that shared-line checking rejects broken KCL and hidden nonlinearity."""
import copy
import unittest

from transmit_interaction import check


def ideal_rows():
    rows = []
    for b in range(25):
        for a in range(25):
            contributions = [-.125 * (3 * (s // 5 - 2) + s % 5 - 2) for s in (a, b)]
            difference = sum(contributions)
            currents = [(v - .5 * difference) / 25 for v in contributions]
            rows.append([a, 2.2 + difference / 2, 2.2 - difference / 2,
                         currents[0] / 2, -currents[0] / 2,
                         currents[1] / 2, -currents[1] / 2, 3.28, -.08])
    return rows


class InteractionCheckerTest(unittest.TestCase):
    def setUp(self):
        self.rows = ideal_rows()

    def test_linear_stages_pass_every_combination(self):
        result = check(self.rows)
        self.assertTrue(result['within_existing_20mV_limit'])
        self.assertLess(result['maximum_per_driver_error_V'], 1e-14)

    def test_balanced_nonlinearity_cannot_hide_in_correct_line_voltage(self):
        # The sum of the two contributions stays unchanged, but each driver
        # introduces an opposite 30 mV error. Checking the line alone misses it.
        self.rows[312][3] += .030 / 25
        self.rows[312][5] -= .030 / 25
        result = check(self.rows)
        self.assertFalse(result['within_existing_20mV_limit'])
        self.assertAlmostEqual(result['maximum_per_driver_error_V'], .030)

    def test_current_conservation_is_required(self):
        self.rows[312][3] += 1e-4
        with self.assertRaisesRegex(ValueError, 'conservation'):
            check(self.rows)

    def test_missing_nonfinite_reordered_and_out_of_rail_data_fail(self):
        for change in ('missing', 'nonfinite', 'order', 'rail', 'supply', 'current'):
            with self.subTest(change=change):
                rows = copy.deepcopy(self.rows)
                if change == 'missing':
                    rows.pop()
                elif change == 'nonfinite':
                    rows[0][1] = float('nan')
                elif change == 'order':
                    rows[1][0] = 2
                elif change == 'rail':
                    rows[0][1] = -.2
                elif change == 'supply':
                    rows[0][7] = 2.8
                else:
                    rows[0][8] = -.2
                with self.assertRaises(ValueError):
                    check(rows)


if __name__ == '__main__':
    unittest.main()
