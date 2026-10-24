"""Reject incomplete receive technology bindings and preserve physical terminals."""
import importlib.util
from pathlib import Path
import unittest

spec = importlib.util.spec_from_file_location('receive_binding',
    Path(__file__).resolve().parents[1] / 'receive_frontend_process.py')
binding = importlib.util.module_from_spec(spec)
spec.loader.exec_module(binding)


def fixture():
    n = lambda beta: (beta, .7, 1)
    p = lambda beta: (beta, .7, -1)
    groups = [('nmos_resistor_amplifier.hpp', [n(.015)] * 2)] * 2
    groups += [('nmos_source_follower.hpp', [n(.1)]),
               ('mos_track_hold.hpp', [n(.08), p(.04), n(.04), p(.02)]),
               ('strongarm_comparator.hpp', [n(.008)] * 3 + [n(.004)] * 2 + [p(.002)] * 6),
               ('cmos_logic.hpp', [p(.01), p(.01), n(.01), n(.01)]),
               ('cmos_logic.hpp', [p(.01), p(.01), n(.01), n(.01)]),
               ('nand_latch.hpp', [n(.05), p(.05)]),
               ('level_down.hpp', [(.0005, .35, -1), n(.004), (.005, .35, -1), (.01, .35, 1)])]
    groups += [('inverter.hpp', [p(.004), n(.004)])]
    groups += [('inverter.hpp', [p(.08), n(.08)])] * 8
    groups += [('inverter.hpp', [p(.01), n(.01)])]
    groups += [('inverter.hpp', [p(.001), n(.001)])] * 2
    groups += [('inverter.hpp', [p(.004), n(.004)])] * 2
    lines, identifier = ['module fixture;', '    analog begin'], 0
    for owner, (filename, parameters) in enumerate(groups):
        for beta, threshold, polarity in parameters:
            source = 'n0' if polarity > 0 else 'n10' if threshold == .35 else 'n9'
            lines += [f'        // device {identifier}, IC IC{owner}, /basic/{filename}:1',
                      f'        I(n{100 + identifier}, {source}) <+ cpphdl_channel(V(n50, {source}), '
                      f'V(n{100 + identifier}, {source}), {beta}, {threshold}, {polarity});']
            identifier += 1
    lines += ['        I(n1, n0) <+ V(n1, n0) / 50;'] * 36
    model = '\n'.join(lines) + '\n    end\nendmodule\n'
    host = ('fixture\n.model CircuitModel fixture\nNdesign 0 CircuitModel\n' +
        ''.join(f'Cdevice{i} n9 0 1f\n' for i in range(72)) +
        'Vsource6 n6 0 PWL(0 1.65 1n 1.53)\nVsource7 n7 0 PWL(0 1.65 1n 1.77)\n' +
        'Vsource11 clear 0 PWL(0 3.3 2n 0)\n.ic V(n9)=3.3\n.control\npre_osdi design.osdi\n' +
        ''.join(f'let observation{i} = v(n{node})\n' for i, node in enumerate((20, 21, 22, 23, 9, 10, 24))) +
        'tran 10p 20n\n.endc\n.end\n')
    return model, host


class ReceiveBindings(unittest.TestCase):
    def test_complete_binding_preserves_charge_and_uses_correct_body_domains(self):
        model, host = fixture()
        deck, parts = binding.process_deck(model, host, Path('/models'), 1)
        self.assertEqual(len(parts), 62)
        self.assertEqual(deck.count('Cdevice'), 72)
        self.assertNotIn('CircuitModel', deck)
        self.assertNotIn('design.osdi', deck)
        self.assertTrue(all(part['body'] == ('0' if part['model'].endswith('nmos') else
                            'n10' if '_lv_' in part['model'] else 'n9') for part in parts))
        sampler = [part for part in parts if part['source'] == 'mos_track_hold.hpp']
        self.assertEqual([part['width_um'] for part in sampler], [100, 200, 50, 100])

    def test_unknown_laws_parameters_and_terminal_changes_are_rejected(self):
        model, host = fixture()
        changed = [model.replace('0.015, 0.7', '0.016, 0.7', 1),
                   model.replace('V(n50, n0)', 'V(n50, n9)', 1),
                   model.replace(' / 50;', ' / 51;', 1),
                   model.replace('    end\n', '        I(n1, n0) <+ sin(V(n1));\n    end\n')]
        for variant in changed:
            with self.subTest(model=variant[:100]), self.assertRaises(ValueError):
                binding.process_deck(variant, host, Path('/models'), 1)

    def test_changed_host_and_hierarchy_cannot_reuse_a_process_selection(self):
        model, host = fixture()
        for variant, deck in ((model.replace('nmos_source_follower.hpp', 'unknown.hpp'), host),
                              (model, host.replace('Cdevice0 n9 0 1f\n', '')),
                              (model, host.replace('let observation4 = v(n9)\n', '')),
                              (model, host.replace('Ndesign ', 'Nunknown '))):
            with self.subTest(host=deck[:100]), self.assertRaises(ValueError):
                binding.process_deck(variant, deck, Path('/models'), 1)

    def test_dc_sweep_keeps_clear_asserted_and_replaces_only_the_differential_stimulus(self):
        model, host = fixture()
        deck, _ = binding.process_deck(model, host, Path('/models'), 1)
        dc = binding.dc_deck(deck)
        self.assertIn('EVsource6 n6 process_center process_difference 0 .5', dc)
        self.assertIn('EVsource7 n7 process_center process_difference 0 -.5', dc)
        self.assertIn('Vsource11 clear 0 3.3', dc)
        self.assertIn('dc Vprocess_difference -.24 .24 .12', dc)
        self.assertNotIn('.ic ', dc)
        with self.assertRaisesRegex(ValueError, 'differential source interface'):
            binding.dc_deck(deck.replace('Vsource6 ', 'Vsource8 '))


if __name__ == '__main__':
    unittest.main()
