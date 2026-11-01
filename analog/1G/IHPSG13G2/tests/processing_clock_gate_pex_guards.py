#!/usr/bin/env python3
"""Keep physical pulse acceptance independent of extracted implementation."""
import unittest

from processing_clock_gate import schedule
from processing_clock_gate_guards import waveform
from processing_clock_gate_pex import qualify


def terminals(pulses=None):
    return [[row[i] for i in (0, 1, 2, 3, 5)] for row in waveform(schedule() if pulses is None else pulses)]


class PhysicalPulseTests(unittest.TestCase):
    def test_complete_contract(self):
        self.assertEqual(qualify(terminals(), 1.2)['pulses'], 32)

    def test_missing_and_cut_pulses(self):
        for pulses in [schedule()[1:], [(a, a + 1e-9) for a, _ in schedule()]]:
            with self.assertRaises(ValueError):
                qualify(terminals(pulses), 1.2)

    def test_delayed_complete_pulses(self):
        with self.assertRaisesRegex(ValueError, '400 ps'):
            qualify(terminals([(a + .5e-9, b + .5e-9) for a, b in schedule()]), 1.2)

    def test_missing_tail_and_supply_droop(self):
        rows = terminals()
        with self.assertRaisesRegex(ValueError, 'incomplete'):
            qualify(rows[:-10], 1.2)
        for row in rows:
            row[4] *= .9
        with self.assertRaisesRegex(ValueError, 'electrical'):
            qualify(rows, 1.2)


if __name__ == '__main__':
    unittest.main()
