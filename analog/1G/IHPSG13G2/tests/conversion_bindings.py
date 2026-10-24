"""Only explicit finite-impedance bench controls may survive PSP binding."""
import importlib.util
import re
from pathlib import Path
import sys
import unittest

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))
from conversion_process import process_deck
spec = importlib.util.spec_from_file_location('acquisition_fixture', Path(__file__).with_name('receive_frontend_bindings.py'))
acquisition = importlib.util.module_from_spec(spec)
spec.loader.exec_module(acquisition)


def fixture():
    model, deck = acquisition.fixture()
    model = model.replace('    end\n', '        // device 900, /test/host.cpp:1\n' +
        ''.join(f'        I(n{300 + i}, n0) <+ (1.0 - V(cpphdl_bit{i})) * V(n{300 + i}, n0) / 50.0; // sim_driver\n'
                f'        I(n{300 + i}, n9) <+ V(cpphdl_bit{i}) * V(n{300 + i}, n9) / 50.0; // sim_driver\n'
                for i in range(9)) + '        I(n1, n0) <+ V(n1, n0) / 50;\n    end\n')
    deck = deck.replace('.control\n', 'Cdevice72 n25 0 20f\n.control\n')
    deck = deck.replace('tran 10p', 'let observation7 = v(n25)\ntran 10p')
    metadata = dict(drivers=[dict(slice=0, bit=i, id=i) for i in range(9)], decisions=['n25'])
    return model, deck, metadata


class ConversionBindings(unittest.TestCase):
    def test_complete_circuit_uses_psp_and_bench_currents_return_to_the_real_rails(self):
        model, original, metadata = fixture()
        deck, parts = process_deck(model, original, Path('/models'), 1, metadata)
        self.assertEqual(len(parts), 62)
        self.assertEqual(deck.count('BhostLow'), 10)  # Nine devices plus the scope comment.
        self.assertIn('BhostHigh0 n300 n9 I=v(bit0)*v(n300,n9)/50', deck)
        self.assertEqual(deck.count('Cdevice'), 73)
        self.assertNotIn('cpphdl_channel', deck)
        self.assertNotIn('design.osdi', deck)
        self.assertTrue(all(p['body'] == ('0' if p['model'].endswith('nmos') else
            'n10' if '_lv_' in p['model'] else 'n9') for p in parts))

    def test_parallel_drivers_require_shared_input_and_output_nodes(self):
        model, deck, metadata = fixture()
        def channel(identifier, owner, filename, drain, gate, source, beta):
            return (f'        // device {identifier}, IC {owner}, /basic/{filename}:1\n'
                f'        I({drain}, {source}) <+ cpphdl_channel(V({gate}, {source}), '
                f'V({drain}, {source}), {beta}, 0.7, 1);\n')
        # Two shared-input amplifier channels, one shared follower; the second
        # original amplifier belongs to the per-slice comparator and stays apart.
        originals = [(0, 'IC0', 'nmos_resistor_amplifier.hpp', 'n30', 'n20', 'n31', .015),
                     (1, 'IC0', 'nmos_resistor_amplifier.hpp', 'n22', 'n21', 'n32', .015),
                     (4, 'IC2', 'nmos_source_follower.hpp', 'n9', 'n22', 'n23', .1)]
        for fields in originals:
            identifier = fields[0]
            pattern = r'        // device ' + str(identifier) + r', [^\n]+\n[^\n]+\n'
            model, replaced = re.subn(pattern, lambda match: channel(*fields), model)
            self.assertEqual(replaced, 1)
        extra = (channel(1000, 'extra_amplifier', 'nmos_resistor_amplifier.hpp', 'n30', 'n20', 'n41', .015)
            + channel(1001, 'extra_amplifier', 'nmos_resistor_amplifier.hpp', 'n22', 'n21', 'n42', .015)
            + channel(1002, 'extra_buffer', 'nmos_source_follower.hpp', 'n9', 'n22', 'n23', .1)
            + '        I(n1, n0) <+ V(n1, n0) / 50;\n' * 6)
        model = model.replace('    end\n', extra + '    end\n')
        deck = deck.replace('.control\n', ''.join(f'Cdevice{i} n9 0 1f\n' for i in range(73, 83)) + '.control\n')
        bound, inventory = process_deck(model, deck, Path('/models'), 1, metadata, input_drivers=2)
        self.assertEqual(len(inventory), 65)
        self.assertEqual(bound.count('Cdevice'), 83)
        for invalid in (model.replace('I(n30, n41)', 'I(n33, n41)').replace('V(n30, n41)', 'V(n33, n41)'),
                        model.replace('V(n22, n23)', 'V(n24, n23)', 1)):
            with self.assertRaises(ValueError):
                process_deck(invalid, deck, Path('/models'), 1, metadata, input_drivers=2)
        with self.assertRaises(ValueError):
            process_deck(model, deck, Path('/models'), 1, metadata)
        for count in (0, 3, True):
            with self.assertRaises(ValueError):
                process_deck(model, deck, Path('/models'), 1, metadata, input_drivers=count)

    def test_unknown_driver_resistance_rail_expression_and_missing_branch_fail(self):
        model, deck, metadata = fixture()
        for changed in (model.replace('/ 50.0;', '/ 51.0;', 1),
                        model.replace('I(n300, n9)', 'I(n300, n10)', 1),
                        model.replace('(1.0 - V(cpphdl_bit0))', '(2.0 - V(cpphdl_bit0))', 1),
                        model.replace('        I(n300, n9) <+ V(cpphdl_bit0) * V(n300, n9) / 50.0; // sim_driver\n', '')):
            with self.assertRaises(ValueError):
                process_deck(changed, deck, Path('/models'), 1, metadata)

    def test_changed_feedback_or_reused_control_identity_fails(self):
        model, deck, metadata = fixture()
        for invalid in (dict(metadata, decisions=['n26']),
                        dict(metadata, drivers=metadata['drivers'][:-1]),
                        dict(metadata, drivers=metadata['drivers'][:-1] + metadata['drivers'][:1])):
            with self.assertRaises(ValueError):
                process_deck(model, deck, Path('/models'), 1, invalid)


if __name__ == '__main__':
    unittest.main()
