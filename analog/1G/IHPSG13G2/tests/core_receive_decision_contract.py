#!/usr/bin/env python3
"""Negative checks for the connected decision timing and exposure contract."""
import unittest

from core_receive_decision import CODES, HEADER, PERIOD, WARMUP, contract, verify


def trajectory():
    rows = []
    for tick in range(6401):
        time = WARMUP + tick * 10e-12
        index = min(tick // 400, 15)
        phase = (tick % 400) * 10e-12
        above = (index % 2 == 0) != (index >= 8)
        row = [0.] * len(HEADER)
        row[0] = time
        row[1] = 3.3 * (CODES[index] + (2.5 if above else -2.5)) / 256
        row[2] = 3.3 * CODES[index] / 256
        row[6] = 3.3 if phase >= 2.3e-9 else 0
        row[10] = 1.2 if above else 0
        row[11:14] = [3.3, 1.2, 2.]
        rows.append(row)
    return rows


class DecisionContract(unittest.TestCase):
    def test_declared_schedule(self):
        result = contract(trajectory())
        verify(result)
        self.assertEqual(result['decision_pass'], [True] * 16)

    def test_wrong_or_late_return(self):
        for value in (0., .6):
            rows = trajectory()
            rows[370][10] = value
            self.assertFalse(contract(rows)['checks']['returned_decisions'])
            with self.assertRaises(ValueError):
                verify(contract(rows))

    def test_wrong_threshold(self):
        rows = trajectory()
        rows[175][2] += .04
        self.assertFalse(contract(rows)['checks']['dac_settling'])

    def test_exposure_between_apertures(self):
        rows = trajectory()
        rows[25][19] = 1.7
        self.assertFalse(contract(rows)['checks']['core_device_exposure'])
        rows = trajectory()
        rows[25][16] = 1.7
        self.assertFalse(contract(rows)['checks']['core_device_exposure'])

    def test_missing_clock_and_clear(self):
        rows = trajectory()
        for row in rows:
            row[6] = 0
        self.assertFalse(contract(rows)['checks']['evaluation_arrives'])
        rows[10][15] = 3.3
        self.assertFalse(contract(rows)['checks']['clear_released'])

    def test_incomplete(self):
        with self.assertRaises(ValueError):
            contract(trajectory()[:-1])
        rows = trajectory()
        rows = [r for r in rows if not WARMUP + 1.5e-9 <= r[0] <= WARMUP + 2e-9]
        with self.assertRaises(ValueError):
            contract(rows)


if __name__ == '__main__':
    unittest.main()
