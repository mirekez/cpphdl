"""Loaded-rail interpolation must preserve input/supply dependence and bounds."""
import importlib.util
from pathlib import Path
import unittest

spec = importlib.util.spec_from_file_location('receiver_dc_surface', Path(__file__).resolve().parents[1] / 'receive_dc_surface.py')
surface = importlib.util.module_from_spec(spec)
spec.loader.exec_module(surface)


class Transfer(unittest.TestCase):
    def rows(self):
        result = []
        for source in surface.SUPPLIES:
            for difference in surface.DIFFERENCES:
                supply = source - .01 - .02 * difference
                result.append([difference, source, 1.65 + difference / 2, 1.65 - difference / 2,
                    2 + 2 * difference + .5 * (supply - 3.3),
                    1.4 + difference + .8 * (supply - 3.3), supply, 1.2])
        return result

    def test_affine_surface_uses_achieved_rail(self):
        model = surface.Surface(self.rows())
        for difference in (-.24, -.173, 0, .037, .24):
            for supply in (3.03, 3.273, 3.3, 3.57):
                drain, output = model(1.65 + difference / 2, 1.65 - difference / 2, supply)
                self.assertAlmostEqual(drain, 2 + 2 * difference + .5 * (supply - 3.3), places=12)
                self.assertAlmostEqual(output, 1.4 + difference + .8 * (supply - 3.3), places=12)

    def test_collapsed_buffer_transfer_fails(self):
        rows = self.rows()
        for row in rows:
            row[5] = 1.4
        with self.assertRaisesRegex(ValueError, 'buffer transfer'):
            surface.Surface(rows)

    def test_missing_mesh_and_uncharacterized_conditions_fail(self):
        with self.assertRaises(ValueError):
            surface.Surface(self.rows()[:-1])
        model = surface.Surface(self.rows())
        for args in ((1.65, 1.65, 2.9), (1.65, 1.65, 3.7), (1.8, 1.8, 3.3), (1.9, 1.4, 3.3)):
            with self.assertRaises(ValueError):
                model(*args)


if __name__ == '__main__':
    unittest.main()
