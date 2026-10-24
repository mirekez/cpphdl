"""Pin geometry semantics independently of transient functional success."""
import importlib.util
from pathlib import Path
import unittest

source = Path(__file__).resolve().parents[1] / 'mos_geometry.py'
spec = importlib.util.spec_from_file_location('ihp_geometry', source)
geometry = importlib.util.module_from_spec(spec)
spec.loader.exec_module(geometry)


class Geometry(unittest.TestCase):
    def test_total_width_and_terminals_survive_folding(self):
        deck, parts = geometry.size_mos_instances('Xtail drain bias source bulk sg13_hv_nmos w=450u l=.45u\n')
        self.assertIn('w=450u l=.45u ng=45', deck)
        self.assertEqual(parts[0]['total_width_um'], 450)
        self.assertEqual(parts[0]['finger_width_um'], 10)
        self.assertEqual([parts[0][p] for p in ('drain', 'gate', 'source', 'body')],
                         ['drain', 'bias', 'source', 'bulk'])

    def test_existing_valid_layout_choice_is_preserved(self):
        text = 'Xp d g s b sg13_hv_pmos w=30u l=.4u ng=6\n'
        deck, parts = geometry.size_mos_instances(text)
        self.assertEqual(deck, text)
        self.assertEqual(parts[0]['finger_width_um'], 5)

    def test_invalid_explicit_fingers_and_dimensions_are_rejected(self):
        for parameters in ('w=450u l=.45u ng=1', 'w=1u l=.44u', 'w=.29u l=.45u',
                           'w=1u l=11u', 'w=1u l=.45u ng=0', 'w=1u l=.45u ng=1.5'):
            with self.subTest(parameters=parameters), self.assertRaises(ValueError):
                geometry.size_mos_instances('Xn d g s b sg13_hv_nmos ' + parameters)

    def test_si_units_and_lv_limits(self):
        _, parts = geometry.size_mos_instances('Xn d g s b sg13_lv_nmos w=40e-6 l=130n\n')
        self.assertEqual(parts[0]['fingers'], 4)
        self.assertAlmostEqual(parts[0]['length_um'], .13)
        with self.assertRaises(ValueError):
            geometry.size_mos_instances('Xn d g s b sg13_lv_nmos w=.14u l=.13u')


if __name__ == '__main__':
    unittest.main()
