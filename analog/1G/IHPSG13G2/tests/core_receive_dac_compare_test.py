#!/usr/bin/env python3
"""Guard against a spike visible on only one simulator's sample grid."""
import unittest

from core_receive_dac_compare import directional_errors


class ComparisonGrid(unittest.TestCase):
    def test_independent_grids_agree_for_linear_signal(self):
        a = [[i * 1e-12, i * .002] for i in range(21)]
        b = [[i * .5e-12, i * .001] for i in range(41)]
        self.assertLess(max(directional_errors(a, b)[0]), 1e-16)
        self.assertLess(max(directional_errors(b, a)[0]), 1e-16)

    def test_reference_only_excursion_is_not_hidden(self):
        native = [[i * 1e-12, 0.] for i in range(21)]
        spice = [[i * .5e-12, .02 if i == 19 else 0.] for i in range(41)]
        self.assertEqual(directional_errors(native, spice)[0], [0.])
        self.assertEqual(directional_errors(spice, native)[0], [.02])

    def test_nonoverlap_and_truncated_end_are_rejected(self):
        rows = [[i * 1e-12, 0.] for i in range(21)]
        with self.assertRaises(ValueError):
            directional_errors(rows, rows[15:])
        with self.assertRaises(ValueError):
            directional_errors(rows, rows[:-1])


if __name__ == '__main__':
    unittest.main()
