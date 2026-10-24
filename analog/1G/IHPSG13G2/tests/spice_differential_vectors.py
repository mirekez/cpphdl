"""Save selection must preserve every equation and each side of checked drops."""
from pathlib import Path
import sys
import unittest

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))
from spice_differential_vectors import keep_observed_differential_voltages

DECK = ('fixture\nR1 n1 n2 100\nC1 n2 0 1p IC=0\n.control\ntran 1p 10n\n'
        'let observation0 = v(n1)\nlet observation1 = v(n3) - v(n2)\n'
        'let observation2 = v(n1) - v(n3)\nwrdata trace.dat observation0 observation1 observation2\n.endc\n.end\n')


class VoltageSelection(unittest.TestCase):
    def test_each_side_is_retained_and_no_equations_or_analysis_change(self):
        result = keep_observed_differential_voltages(DECK)
        self.assertEqual(result.replace('.save v(n1) v(n3) v(n2)\n', ''), DECK)
        self.assertEqual(result.count('.save '), 1)

    def test_unknown_partial_expressions_reordered_and_existing_saves_fail(self):
        for altered in (DECK.replace('v(n3) - v(n2)', 'v(n3) + v(n2)'),
                        DECK.replace('v(n3) - v(n2)', 'v(n3) - v(n2) + 1'),
                        DECK.replace('v(n1)', 'i(Vsource0)'),
                        DECK.replace('observation1', 'observation4'),
                        DECK.replace('.control\n', '.save v(n1)\n.control\n'),
                        DECK.replace('.control\n', '.control\n.control\n')):
            with self.subTest(deck=altered), self.assertRaises(ValueError):
                keep_observed_differential_voltages(altered)


if __name__ == '__main__':
    unittest.main()
