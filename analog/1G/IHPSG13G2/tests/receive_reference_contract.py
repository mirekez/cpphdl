#!/usr/bin/env python3
"""Exercise reference acceptance boundaries without invoking either solver."""
import copy
import unittest

from receive_reference import contract, verify


class ReferenceContract(unittest.TestCase):
    @staticmethod
    def rows(ratio, load_profile):
        levels = ([1.1, 1.4, 1.65, 1.9, 1.1, 1.9, 1.4, 1.65]
                  if load_profile == 'wide' else [.9, 1.2, 1.35, 1.5, .9, 1.5, 1.2, 1.35])
        target = 3.3 * ratio
        return [[16e-6 + (32 * slot + 7 + sample * .1) * 1e-9,
                 target, 3.3, target, 2.5, level, .6, 2.1, .9]
                for slot, level in enumerate(levels) for sample in range(200)]

    def test_both_classes_and_loads(self):
        for ratio in (.5, 1000 / 2275):
            for load in ('wide', 'receiver'):
                with self.subTest(ratio=ratio, load=load):
                    result = contract(self.rows(ratio, load), ratio, load)
                    verify(result)
                    self.assertEqual(result['loaded_channels'], 8)
                    self.assertEqual(result['total_load_resistance_Ohm'], 112.5)

    def test_accuracy_limit_is_identical_for_both_loads(self):
        for load in ('wide', 'receiver'):
            with self.subTest(load=load):
                rows = self.rows(1000 / 2275, load)
                for row in rows:
                    row[1] += .016
                result = contract(rows, 1000 / 2275, load)
                self.assertFalse(result['checks']['settled_accuracy'])
                with self.assertRaisesRegex(ValueError, 'settled_accuracy'):
                    verify(result)

    def test_physical_faults_are_rejected(self):
        cases = [('transient_excursion', 1, .101), ('divider_ratio', 3, .003),
                 ('supply', 2, -.2), ('drive_headroom', 4, 1.), ('excitation', 5, .1)]
        for check, column, delta in cases:
            with self.subTest(check=check):
                rows = copy.deepcopy(self.rows(.5, 'receiver'))
                for row in rows:
                    row[column] += delta
                result = contract(rows, .5, 'receiver')
                self.assertFalse(result['checks'][check])
                with self.assertRaises(ValueError):
                    verify(result)

    def test_missing_plateau(self):
        with self.assertRaisesRegex(ValueError, 'missing reference load-step aperture'):
            contract(self.rows(.5, 'receiver')[:-200], .5, 'receiver')

    def test_load_profile_cannot_be_mislabelled(self):
        result = contract(self.rows(.5, 'wide'), .5, 'receiver')
        self.assertFalse(result['checks']['excitation'])
        with self.assertRaises(ValueError):
            contract(self.rows(.5, 'wide'), .5, 'unknown')

    def test_divider_class_cannot_be_mislabelled(self):
        result = contract(self.rows(.5, 'receiver'), 1000 / 2275, 'receiver')
        self.assertFalse(result['checks']['settled_accuracy'])
        self.assertFalse(result['checks']['divider_ratio'])


if __name__ == '__main__':
    unittest.main()
