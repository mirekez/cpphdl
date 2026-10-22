"""A reduced waveform selection must preserve equations and every observation."""
import importlib.util
from pathlib import Path
import unittest

spec = importlib.util.spec_from_file_location('spice_voltage_selection',
    Path(__file__).resolve().parents[1] / 'spice_vectors.py')
vectors = importlib.util.module_from_spec(spec)
spec.loader.exec_module(vectors)

DECK = ('fixture\nR1 n1 n2 100\nC1 n2 0 1p IC=0\n.ic V(n2)=0\n.control\n'
        'tran 1p 10n\nlet observation0 = v(n1)\nlet observation1 = v(n2)\n'
        'let observation2 = v(n1)\nwrdata trace.dat observation0 observation1 observation2\nquit\n.endc\n.end\n')


class SpiceVectors(unittest.TestCase):
    def test_only_one_save_line_is_added_and_all_checked_nodes_are_retained(self):
        result = vectors.keep_observed_voltages(DECK)
        self.assertEqual(result.replace('.save v(n1) v(n2)\n', ''), DECK)
        self.assertEqual(result.count('.save '), 1)

    def test_unknown_observations_and_existing_selections_are_rejected(self):
        for changed in (DECK.replace('v(n2)', 'i(Vsource0)'),
                        DECK.replace('observation1', 'observation4'),
                        DECK.replace('.control\n', '.save v(n1)\n.control\n'),
                        DECK.replace('.control\n', '.control\n.control\n')):
            with self.subTest(deck=changed), self.assertRaises(ValueError):
                vectors.keep_observed_voltages(changed)


if __name__ == '__main__':
    unittest.main()
