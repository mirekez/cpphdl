#!/usr/bin/env python3
"""Reject lost, extra and cut pulses in the composed delivery checker."""
import math
import unittest
from unittest.mock import patch

import processing_clock_delivery as delivery


class DeliveryChecks(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        # Isolate the new admission checks. The imported oscillator/controller
        # checker retains its separate complete-trajectory regressions.
        cls.rows = [[0, 0, .4, .6, 1.2, 0, 0, 0, 0],
                    [58.9e-6, 0, .4, .6, 1.2, 0, 0, 0, 0],
                    [59e-6, 0, .4, .6, 1.2, 0, 0, 1.2, 1.2]]
        period = 1 / 134e6
        def clock(time):
            if time <= 60e-6:
                return 0.0
            phase = (time - 60e-6) % period
            return 1.2 * min(1, max(0, phase / 50e-12),
                             max(0, (period / 2 + 50e-12 - phase) / 50e-12))
        for index in range(160001):
            time = 60e-6 + index * 25e-12
            cls.rows.append([time, clock(time), .4, .6, 1.2, 0,
                             clock(time - 100e-12), 1.2, 1.2])

    def qualify(self, rows):
        with patch.object(delivery, 'qualify_oscillator', return_value={'frequency_Hz': 134e6}):
            return delivery.qualify(rows, [], 'cpp')

    def test_complete_delivered_pulses(self):
        result = self.qualify(self.rows)
        self.assertGreaterEqual(result['delivered_pulses'], 535)
        self.assertAlmostEqual(result['maximum_edge_delay_s'], 100e-12, delta=1e-14)

    def test_missing_settled_pulse(self):
        rows = [row.copy() if 60.1e-6 < row[0] < 60.108e-6 else row for row in self.rows]
        for row in rows:
            if 60.1e-6 < row[0] < 60.108e-6:
                row[6] = 0
        with self.assertRaises(ValueError):
            self.qualify(rows)

    def test_cut_high_pulse(self):
        rows = list(self.rows)
        index = next(i for i, row in enumerate(rows) if row[0] > 60.001e-6 and row[6] > 1)
        rows[index] = rows[index].copy()
        rows[index][6] = 0
        with self.assertRaises(ValueError):
            self.qualify(rows)

    def test_output_before_admission(self):
        rows = [self.rows[0], [1e-6, 0, .4, .6, 1.2, 0, 1.2, 0, 0], *self.rows[1:]]
        with self.assertRaisesRegex(ValueError, 'before physical admission'):
            self.qualify(rows)

    def test_nonfinite_voltage(self):
        rows = list(self.rows)
        rows[-1] = rows[-1].copy()
        rows[-1][6] = math.nan
        with self.assertRaisesRegex(ValueError, 'invalid clock delivery'):
            self.qualify(rows)


if __name__ == '__main__':
    unittest.main()
