#!/usr/bin/env python3
"""Check that reference acceptance rejects incomplete and corrupted evidence."""
import math
import unittest

from receive_common_reference import contract, verify


class CommonReferenceContract(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        # An independent continuous-time ramp solution, not a second call to
        # the checker's piecewise-linear recurrence. These are checker vectors,
        # not evidence that the transistor reference circuit works.
        cls.rows = []
        duration = 256e-9
        common_tau = 150e-15 * (500 * 150 / 650)
        difference_tau = 150e-15 / (1 / 500 + 1 / 150 + 2 / 100)
        initial_common = (150 * 2.08 + 500 * 1.8) / 650
        common_slope = (150 / 650) * .39 / duration
        difference_slope = (3 / 43) * 2 / duration
        for i in range(32001):
            t = i * 8e-12
            mean = initial_common + common_slope * (t - common_tau * (1 - math.exp(-t / common_tau)))
            difference = -3 / 43 + difference_slope * (t - difference_tau * (1 - math.exp(-t / difference_tau)))
            terminal_mean, terminal_difference = 2.08 + .39 * t / duration, -1 + 2 * t / duration
            row = [16e-6 + t, 1.8, 3.3, 1.8, 1.3, 1., 2., 1.]
            for lane in range(8):
                sign = 1 if lane % 2 == 0 else -1
                row += [terminal_mean + sign * terminal_difference / 2,
                        terminal_mean - sign * terminal_difference / 2,
                        mean + sign * difference / 2, mean - sign * difference / 2]
            cls.rows.append(row)

    def changed(self, index, values):
        rows = self.rows.copy()
        rows[index] = rows[index].copy()
        for column, value in values.items():
            rows[index][column] = value
        return rows

    def rejected(self, rows, check):
        result = contract(rows)
        self.assertFalse(result['checks'][check], result)
        with self.assertRaises(ValueError):
            verify(result)

    def test_independent_ramp_solution(self):
        result = contract(self.rows)
        verify(result)
        self.assertLess(result['maximum_common_rc_error_V'], 1e-10)
        self.assertLess(result['maximum_differential_rc_error_V'], 1e-10)

    def test_missing_interval_and_settled_window(self):
        for rows in (self.rows[2:], self.rows[:-2]):
            with self.subTest(boundary=rows[0][0]):
                with self.assertRaisesRegex(ValueError, 'observation interval'):
                    contract(rows)
        rows = [row for row in self.rows if not 16.006e-6 <= row[0] <= 16.007e-6]
        with self.assertRaisesRegex(ValueError, 'settled aperture'):
            contract(rows)

    def test_short_excursions_are_not_hidden_by_settled_apertures(self):
        # 2 ns is outside every settled aperture; single states still count.
        for column, value, check in ((1, 1.699, 'transient_reference'),
                                     (2, 3.19, 'supply'), (4, .79, 'drive'),
                                     (3, 1.803, 'divider')):
            with self.subTest(check=check):
                self.rejected(self.changed(250, {column: value}), check)
        self.rejected(self.changed(800, {1: 1.784}), 'settled_reference')

    def test_last_lane_rc_and_common_mode_are_checked(self):
        plus, minus = 38, 39
        row = self.rows[250]
        self.rejected(self.changed(250, {plus: row[plus] + .001, minus: row[minus] + .001}),
                      'common_rc_equation')
        self.rejected(self.changed(250, {plus: row[plus] + .001, minus: row[minus] - .001}),
                      'differential_rc_equation')
        for mean in (1.799, 2.001):
            with self.subTest(mean=mean):
                self.rejected(self.changed(250, {plus: mean, minus: mean}), 'input_common_mode')

    def test_every_lane_must_be_excited_and_the_sink_loaded(self):
        rows = [row[:36] + [2.3, 2.3] + row[38:] for row in self.rows]
        self.rejected(rows, 'differential_excitation')
        self.rejected(self.changed(250, {column: 1.8 for lane in range(8)
                                        for column in (10 + 4 * lane, 11 + 4 * lane)}), 'sink_loading')

    def test_malformed_states(self):
        for rows in (self.changed(250, {1: math.nan}),
                     self.changed(250, {0: self.rows[249][0]}),
                     [row[:-1] for row in self.rows]):
            with self.assertRaises(ValueError):
                contract(rows)


if __name__ == '__main__':
    unittest.main()
