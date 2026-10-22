"""Preserve follower body-current observations and reject unbound device laws."""
import importlib.util
from pathlib import Path
import unittest

spec = importlib.util.spec_from_file_location('follower_process',
    Path(__file__).resolve().parents[1] / 'follower_process.py')
binding = importlib.util.module_from_spec(spec)
spec.loader.exec_module(binding)

MODEL = '''module fixture;
    analog begin
        I(n9, n12) <+ cpphdl_channel(V(n11, n12), V(n9, n12), .05, .7, 1);
        I(n2, n9) <+ V(n2, n9) / 10;
        I(n4, n11) <+ V(n4, n11) / 50;
        I(n12, n0) <+ V(n12, n0) / 500;
        I(n12, n0) <+ V(n12, n0) / 10000;
    end
endmodule
'''
HOST = ('fixture\n.model CircuitModel dummy\nNdesign 0 n2 n9 CircuitModel\n' +
        ''.join(f'Cdevice{i} n9 0 1f\n' for i in range(5)) +
        'Vsource0 n4 0 PWL(0 0 1n 2.5)\n.ic V(n9)=0\n.control\npre_osdi design.osdi\n' +
        '\n'.join(f'let observation{i} = v(n{i + 9})' for i in range(4)) +
        '\ntran 5p 204n\n.endc\n.end\n')


class FollowerBinding(unittest.TestCase):
    def test_real_four_terminal_device_and_zero_bias_body_probe(self):
        deck = binding.process_deck(MODEL, HOST, Path('/models'), 'mos_tt', 27)
        self.assertIn('n9 n11 n12 follower_body sg13_hv_nmos w=200u l=.45u', deck)
        self.assertEqual(deck.count('Vfollower_body follower_body 0 0'), 1)
        self.assertEqual(deck.count('Cdevice'), 5)
        self.assertEqual(deck.count('Xphysical'), 1)
        self.assertNotIn('CircuitModel', deck)
        self.assertNotIn('design.osdi', deck)
        dc = binding.dc_deck(deck)
        self.assertIn('Vsource0 n4 0 2.1', dc)
        self.assertIn('dc Vsource0 2.1 2.9 .2', dc)
        self.assertIn('observation0 observation1 observation2 i(Vfollower_body)', dc)
        self.assertNotIn('.ic ', dc)
        self.assertNotIn('tran ', dc)

    def test_changed_terminal_physics_and_passives_are_rejected(self):
        for changed in (MODEL.replace('.05, .7', '.06, .7'),
                        MODEL.replace('V(n11, n12)', 'V(n11, n0)'),
                        MODEL.replace(' / 500;', ' / 501;'),
                        MODEL.replace('V(n2, n9) /', 'V(n4, n9) /'),
                        MODEL.replace('    end\n', '        I(n1, n2) <+ sin(V(n1));\n    end\n')):
            with self.subTest(model=changed), self.assertRaises(ValueError):
                binding.process_deck(changed, HOST, Path('/models'), 'mos_tt', 27)

    def test_incomplete_inventory_and_unsupported_analysis_are_rejected(self):
        for host, corner, temperature in (
                (HOST.replace('Cdevice0 n9 0 1f\n', ''), 'mos_tt', 27),
                (HOST.replace('Ndesign ', 'Nother '), 'mos_tt', 27),
                (HOST, 'unknown', 27), (HOST, 'mos_tt', float('nan'))):
            with self.subTest(corner=corner, temperature=temperature), self.assertRaises(ValueError):
                binding.process_deck(MODEL, host, Path('/models'), corner, temperature)
        deck = binding.process_deck(MODEL, HOST, Path('/models'), 'mos_tt', 27)
        with self.assertRaisesRegex(ValueError, 'source or observation interface'):
            binding.dc_deck(deck.replace('let observation3', 'let hidden3'))


if __name__ == '__main__':
    unittest.main()
