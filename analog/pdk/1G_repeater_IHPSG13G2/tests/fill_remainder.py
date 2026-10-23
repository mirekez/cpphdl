"""Extra fill must obey geometry and exclusion contracts before full DRC."""
import importlib.util
from pathlib import Path
import unittest
import pya

spec = importlib.util.spec_from_file_location('module_fill_remainder', Path(__file__).resolve().parents[1] / 'fill_remainder.py')
fill = importlib.util.module_from_spec(spec)
spec.loader.exec_module(fill)


class Remainder(unittest.TestCase):
    def test_density_width_spacing_and_all_keepouts(self):
        for number, function in ((10, fill.add_metal2_remainder), (30, fill.add_metal3_remainder),
                                 (50, fill.add_metal4_remainder)):
            with self.subTest(layer=number):
                self.check_geometry(number, function)

    def check_geometry(self, number, function):
        layout = pya.Layout()
        layout.dbu = .001
        top = layout.create_cell('dut')
        top.shapes(layout.layer(189, 0)).insert(pya.Box(0, 0, 50000, 50000))
        drawing = pya.Box(10000, 0, 11000, 50000)
        top.shapes(layout.layer(number, 0)).insert(drawing)
        top.shapes(layout.layer(26, 0)).insert(pya.Box(20000, 20000, 22000, 22000))
        for blocked_layer, datatype, x in ((number, 23, 25000), (number, 24, 30000), (160, 0, 35000)):
            top.shapes(layout.layer(blocked_layer, datatype)).insert(pya.Box(x, 10000, x + 2000, 40000))
        result = function(layout, top)
        self.assertGreaterEqual(result['final_percent'], 35)
        self.assertGreater(result['added_area_um2'], 0)
        region = pya.Region(top.begin_shapes_rec(layout.layer(number, 22)))
        self.assertTrue(region.width_check(1000).is_empty())
        self.assertTrue(region.space_check(420).is_empty())
        self.assertTrue(region.separation_check(pya.Region(drawing), 420).is_empty())
        trans = pya.Region(top.begin_shapes_rec(layout.layer(26, 0)))
        self.assertTrue(region.separation_check(trans, 1000).is_empty())
        for blocked_layer, datatype in ((number, 23), (number, 24), (160, 0)):
            keepout = pya.Region(top.begin_shapes_rec(layout.layer(blocked_layer, datatype)))
            self.assertTrue((region & keepout).is_empty())
        self.assertTrue((region - pya.Region(pya.Box(0, 0, 50000, 50000))).is_empty())
        self.assertEqual(len(layout.top_cells()), 1)

    def test_empty_layout_is_rejected(self):
        layout = pya.Layout()
        with self.assertRaises(ValueError):
            fill.add_metal2_remainder(layout, layout.create_cell('empty'))


if __name__ == '__main__':
    unittest.main()
