#!/usr/bin/env python3
"""Reject truncated pulses and acquisition leakage even with correct edge counts."""
from bisect import bisect_right
import math
import unittest

from processing_clock_gate import STOP, clock_schedule, qualify, schedule


def waveform(pulses):
    transitions = sorted(value for pulse in pulses for value in pulse)
    clocks = sorted(value for pulse in clock_schedule() for value in pulse)
    rows = []
    for index in range(math.ceil(STOP / 10e-12) + 1):
        time = min(index * 10e-12, STOP)
        value = 1.2 if bisect_right(transitions, time) % 2 else 0
        raw = 1.2 if bisect_right(clocks, time) % 2 else 0
        rows.append([time, raw, 0, value, 0, 1.2])
    return rows


class AdmissionGuards(unittest.TestCase):
    def test_complete_pulses(self):
        self.assertEqual(qualify(waveform(schedule()), 1.2)['pulses'], 32)

    def test_cut_high_phase_with_same_edge_count(self):
        pulses = schedule()
        pulses[0] = (pulses[0][0], pulses[0][0] + 1e-9)
        with self.assertRaisesRegex(ValueError, 'runt'):
            qualify(waveform(pulses), 1.2)

    def test_starting_in_high_phase_with_same_edge_count(self):
        pulses = schedule()
        pulses[0] = tuple(time + 1e-9 for time in pulses[0])
        with self.assertRaisesRegex(ValueError, 'wrong phase'):
            qualify(waveform(pulses), 1.2)

    def test_raw_acquisition_clock_leakage(self):
        with self.assertRaisesRegex(ValueError, 'extra, missing'):
            qualify(waveform([(22.5e-9, 23.75e-9)] + schedule()), 1.2)

    def test_absent_raw_clock_stress(self):
        rows = waveform(schedule())
        for row in rows:
            row[1] = 0
        with self.assertRaisesRegex(ValueError, 'stimulus'):
            qualify(rows, 1.2)

    def test_incomplete_or_unresolved_waveform(self):
        rows = waveform(schedule())
        for bad in (rows[:-10], rows[::2], rows[2:]):
            with self.subTest(length=len(bad)), self.assertRaisesRegex(ValueError, 'incomplete'):
                qualify(bad, 1.2)


if __name__ == '__main__':
    unittest.main()
