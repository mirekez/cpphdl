"""Regression vectors for explicit targets; these are not circuit evidence."""
import unittest

import receive_common_contract as fixture
from receive_common_reference import contract as original_contract, verify
from receive_common_target import contract


class ExplicitTargetContract(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        fixture.CommonReferenceContract.setUpClass()
        cls.original = fixture.CommonReferenceContract.rows
        cls.target = 3.3 * 243 / 443
        delta = cls.target - 1.8
        cls.rows = [row.copy() for row in cls.original]
        for row in cls.rows:
            row[1] += delta
            row[3] += delta
            for lane in range(8):
                row[10 + 4 * lane] += 500 / 650 * delta
                row[11 + 4 * lane] += 500 / 650 * delta

    def evaluate(self, rows):
        return contract(rows, numerator=243, denominator=443)

    def test_original_exact_parity(self):
        actual = contract(self.original, numerator=6, denominator=11)
        actual.pop('declared_target')
        self.assertEqual(actual, original_contract(self.original))

    def test_declared_target(self):
        result = self.evaluate(self.rows)
        verify(result)
        self.assertLess(result['maximum_settled_error_V'], 1e-14)
        self.assertLess(result['maximum_common_rc_error_V'], 1e-10)
        self.assertFalse(original_contract(self.rows)['checks']['divider'])

    def test_unchanged_limits_and_input_bounds(self):
        for index, changes, check in (
                (800, {1: self.target + .015001}, 'settled_reference'),
                (250, {1: self.target - .100001}, 'transient_reference'),
                (250, {3: self.target + .002001}, 'divider'),
                (250, {38: 1.799, 39: 1.799}, 'input_common_mode'),
                (250, {38: 2.001, 39: 2.001}, 'input_common_mode')):
            with self.subTest(check=check, changes=changes):
                rows = self.rows.copy()
                rows[index] = rows[index].copy()
                for column, value in changes.items():
                    rows[index][column] = value
                self.assertFalse(self.evaluate(rows)['checks'][check])

    def test_missing_evidence_and_invalid_target(self):
        with self.assertRaises(ValueError):
            self.evaluate(self.rows[:-2])
        for numerator, denominator in ((0, 11), (11, 11), (6., 11), (6, -11)):
            with self.assertRaises(ValueError):
                contract(self.rows, numerator=numerator, denominator=denominator)
        with self.assertRaises(TypeError):
            contract(self.rows)


if __name__ == '__main__':
    unittest.main()
