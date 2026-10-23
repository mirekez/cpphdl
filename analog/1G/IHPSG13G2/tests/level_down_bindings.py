"""Process binding must preserve terminals and reject unsupported physics."""
import importlib.util
from pathlib import Path
import unittest

spec = importlib.util.spec_from_file_location('level_down_process', Path(__file__).resolve().parents[1] / 'level_down_process.py')
binding = importlib.util.module_from_spec(spec)
spec.loader.exec_module(binding)

MODEL = '''module fixture;
    analog begin
        I(n13, n9) <+ cpphdl_channel(V(n0, n9), V(n13, n9), .0005, .35, -1);
        I(n13, n0) <+ cpphdl_channel(V(n11, n0), V(n13, n0), .004, .7, 1);
        I(n12, n9) <+ cpphdl_channel(V(n13, n9), V(n12, n9), .005, .35, -1);
        I(n12, n0) <+ cpphdl_channel(V(n13, n0), V(n12, n0), .01, .35, 1);
        I(n2, n9) <+ V(n2, n9) / 10;
        I(n4, n11) <+ V(n4, n11) / 50;
        I(n0, n12) <+ V(n0, n12) / 10000;
    end
endmodule
'''
HOST = ('fixture\n.model CircuitModel dummy\nNdesign 0 n2 n9 CircuitModel\n' +
        ''.join(f'Cdevice{i} n9 0 1f\n' for i in range(9)) +
        '.control\npre_osdi design.osdi\ntran 2p 32n\n.endc\n.end\n')


class Bindings(unittest.TestCase):
    def test_explicit_geometries_and_real_source_body_connections(self):
        deck = binding.process_deck(MODEL, HOST, Path('/models'), 'mos_tt', 27)
        self.assertIn('n13 0 n9 n9 sg13_lv_pmos w=2u l=0.13u', deck)
        self.assertIn('n13 n11 0 0 sg13_hv_nmos w=5u l=0.45u', deck)
        self.assertEqual(deck.count('Cdevice'), 9)
        self.assertEqual(deck.count('Xphysical'), 4)
        self.assertNotIn('CircuitModel', deck)
        self.assertNotIn('design.osdi', deck)

    def test_changed_channels_and_passive_laws_are_rejected(self):
        for changed in (MODEL.replace('.0005', '.0006'),
                        MODEL.replace('V(n11, n0)', 'V(n11, n9)'),
                        MODEL.replace(' / 10;', ' / 11;'),
                        MODEL.replace('        I(n2, n9)', '        I(n1, n9)'),
                        MODEL.replace('    end\n', '        I(n1, n2) <+ sin(V(n1));\n    end\n')):
            with self.subTest(model=changed), self.assertRaises(ValueError):
                binding.process_deck(changed, HOST, Path('/models'), 'mos_tt', 27)

    def test_incomplete_inventory_charge_and_analysis_are_rejected(self):
        for model, host, corner, temperature in (
                (MODEL, HOST.replace('Cdevice0 n9 0 1f\n', ''), 'mos_tt', 27),
                (MODEL, HOST.replace('Ndesign ', 'Nother '), 'mos_tt', 27),
                (MODEL.replace('        I(n13, n0) <+ cpphdl_channel(V(n11, n0), V(n13, n0), .004, .7, 1);\n', ''), HOST, 'mos_tt', 27),
                (MODEL, HOST, 'unknown', 27), (MODEL, HOST, 'mos_tt', float('nan'))):
            with self.subTest(corner=corner, temperature=temperature), self.assertRaises(ValueError):
                binding.process_deck(model, host, Path('/models'), corner, temperature)


if __name__ == '__main__':
    unittest.main()
