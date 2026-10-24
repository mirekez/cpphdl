#!/usr/bin/env python3
"""Check DAC acceptance with synthetic ideal-converter and corrupted evidence."""
import math
import unittest

from core_receive_dac import CODES, contract, verify


class DACContract(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        # These ideal states exercise the checker, not the transistor circuit.
        cls.rows = []
        for i in range(6401):
            index = min(i // 400, 15)
            code = CODES[index]
            row = [8e-6 + i * 10e-12, 3.3 * code / 256, 3.3, 1.2, 1.9]
            for bit in range(8):
                value = (code >> bit) & 1
                row += [1.2 * value, 3.3 * (1 - value), 1.2 * (1 - value), 0, 0]
            cls.rows.append(row)

    def changed(self, index, column, value):
        rows = self.rows.copy()
        rows[index] = rows[index].copy()
        rows[index][column] = value
        return rows

    def reject(self, rows, name):
        result = contract(rows)
        self.assertFalse(result['checks'][name])
        with self.assertRaises(ValueError):
            verify(result)

    def test_ideal_oracle(self):
        result = contract(self.rows)
        verify(result)
        self.assertLess(result['maximum_aperture_error_counts'], 1e-12)

    def test_single_state_dac_error_and_last_bit(self):
        i = 1360  # Inside the code-128 decision aperture.
        self.reject(self.changed(i, 1, self.rows[i][1] + 2.01 * 3.3 / 256), 'settling')
        self.reject(self.changed(i, 40, 0), 'core_commands')
        self.reject(self.changed(i, 41, 3.3), 'translated_rails')
        self.reject(self.changed(20, 44, 1.651), 'core_device_exposure')

    def test_input_pmos_drain_source_exposure(self):
        # A negative inverter output can exceed the PMOS drain-to-core-supply
        # limit while every voltage relative to ground remains within it.
        self.reject(self.changed(20, 42, -.451), 'core_device_exposure')

    def test_between_aperture_supplies_and_bias(self):
        for column, value, check in ((2, 2.99, 'io_supply'), (3, 1.079, 'core_supply'),
                (4, .799, 'bias'), (1, -.101, 'output_range')):
            with self.subTest(check=check):
                self.reject(self.changed(20, column, value), check)

    def test_incomplete_or_malformed_states(self):
        for rows in (self.rows[3:], self.rows[:-2], self.changed(200, 1, math.nan),
                [r[:-1] for r in self.rows],
                [r for r in self.rows if not 8.00549e-6 <= r[0] <= 8.00601e-6]):
            with self.assertRaises(ValueError):
                contract(rows)


if __name__ == '__main__':
    unittest.main()
