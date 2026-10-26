#!/usr/bin/env python3
"""Negative checks for the raw-clock acquisition qualification."""
import math
import unittest

from processing_clock_limits import measure, supply_deck


class AcquisitionLimits(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.rows = []
        for index in range(102401):
            time = index * 5e-12
            active = time >= 32.1e-9
            value = 1.2 if active and math.sin(2 * math.pi * (time - 32.1e-9) / 1.6e-9) >= 0 else 0
            cls.rows.append([time, value, value, .6, 1.2, 0 if active else 1.2])

    def test_fast_clock_passes_only_the_new_budget(self):
        self.assertGreater(measure(self.rows, 1.2)['maximum_instantaneous_Hz'], 620e6)
        with self.assertRaisesRegex(ValueError, 'exceeds acquisition'):
            measure(self.rows, 1.2, 400e6)

    def test_short_first_period_is_not_discarded(self):
        rows = [r[:] for r in self.rows]
        for row in rows:
            if 32.4e-9 <= row[0] < 32.5e-9:
                row[1] = 0
        with self.assertRaisesRegex(ValueError, 'exceeds acquisition'):
            measure(rows, 1.2)

    def test_truncated_or_coarse_trace_fails(self):
        with self.assertRaisesRegex(ValueError, 'incomplete'):
            measure(self.rows[:-10], 1.2)
        with self.assertRaisesRegex(ValueError, 'unresolved'):
            measure(self.rows[::2], 1.2)

    def test_missing_and_weak_clock_fail(self):
        with self.assertRaisesRegex(ValueError, 'start and sustain'):
            measure([[r[0], 0, *r[2:]] for r in self.rows], 1.2)
        with self.assertRaisesRegex(ValueError, 'rail levels'):
            measure([[r[0], r[1] * .7, *r[2:]] for r in self.rows], 1.2)

    def test_midscale_may_be_slow_or_stopped_but_never_overspeed(self):
        quiet = [[r[0], 0, *r[2:]] for r in self.rows]
        self.assertEqual(measure(quiet, 1.2, require_start=False)['state'], 'stalled_midscale')
        slow = []
        for row in self.rows:
            time = row[0]
            level = 1.2 if time >= 32.1e-9 and math.sin(2 * math.pi * (time - 32.1e-9) / 80e-9) >= 0 else 0
            slow.append([time, level, *row[2:]])
        self.assertLess(measure(slow, 1.2, require_start=False)['frequency_Hz'], 13e6)
        with self.assertRaisesRegex(ValueError, 'exceeds acquisition'):
            measure(self.rows, 1.2, 400e6, require_start=False)

    def test_supply_conversion_is_explicit_and_complete(self):
        original = '\n'.join(f'Vsource{i} n{i + 1} 0 DC {1.2 if i % 2 else 0}' for i in range(17))
        original += '\nVsource17 n18 0 PWL(0 1.2 3.2e-8 1.2 3.21e-8 0)\n'
        scaled = supply_deck(original, 1.32)
        self.assertNotIn(' 1.2', scaled)
        self.assertIn('DC 1.3200000000000001', scaled)
        with self.assertRaisesRegex(ValueError, 'missing'):
            supply_deck(original.replace('Vsource0 n1 0 DC 0\n', ''), 1.32)
        with self.assertRaisesRegex(ValueError, 'unexpected'):
            supply_deck(original.replace('DC 1.2', 'DC 1.3'), 1.32)


if __name__ == '__main__':
    unittest.main()
