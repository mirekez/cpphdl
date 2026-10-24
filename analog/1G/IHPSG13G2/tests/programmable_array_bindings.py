"""Process binding rejects altered topology, missing charge and unknown devices."""
import argparse
from pathlib import Path
import re
import subprocess
import sys
import tempfile
import unittest

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))
from programmable_array_process import process_deck

parser = argparse.ArgumentParser()
parser.add_argument('--bench', type=Path, required=True)
parser.add_argument('--dummy-p-width-um', type=int, choices=(25, 30), default=25)
parser.add_argument('--charge-defined-nodes', action='store_true')
args, remaining = parser.parse_known_args()


class Bindings(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.temporary = tempfile.TemporaryDirectory(prefix='cpphdl-programmable-receive-bindings-')
        cls.work = Path(cls.temporary.name)
        subprocess.run([args.bench.resolve(), cls.work, 'export', '4'], check=True, timeout=30)
        cls.model = (cls.work / 'design.va').read_text()
        cls.original = (cls.work / 'bench.cir').read_text()

    @classmethod
    def tearDownClass(cls):
        cls.temporary.cleanup()

    def bind(self, model=None, deck=None):
        return process_deck(self.model if model is None else model,
                            self.original if deck is None else deck, self.work / 'models',
                            dummy_p_width_um=args.dummy_p_width_um, charge_defined_nodes=args.charge_defined_nodes)

    def test_reviewed_geometry_and_body_rails(self):
        deck, inventory = self.bind()
        self.assertEqual(len(inventory), 970)
        self.assertNotIn('CircuitModel', deck)
        sampler_widths = [part['width_um'] for part in inventory if part['source'] == 'mos_track_hold.hpp']
        self.assertEqual(sampler_widths, [25, 50, 12.5, args.dummy_p_width_um] * 16)
        observations = dict(re.findall(r'let observation(\d+) = v\((n\d+)\)', self.original))
        for part in inventory:
            if part['model'].endswith('_nmos'):
                self.assertEqual(part['body'], '0')
            else:
                self.assertEqual(part['body'], observations['5' if '_lv_' in part['model'] else '4'])

    def test_changed_termination_is_rejected(self):
        self.assertIn(' / 100.0;', self.model)
        with self.assertRaisesRegex(ValueError, 'resistor'):
            self.bind(model=self.model.replace(' / 100.0;', ' / 110.0;', 1))

    def test_other_dummy_width_cannot_be_bound_to_this_export(self):
        width = 30 if args.dummy_p_width_um == 25 else 25
        with self.assertRaisesRegex(ValueError, 'sampler'):
            process_deck(self.model, self.original, self.work / 'models', dummy_p_width_um=width,
                         charge_defined_nodes=args.charge_defined_nodes)

    def test_charge_storage_profile_cannot_be_switched(self):
        with self.assertRaisesRegex(ValueError, 'inventory|storage'):
            process_deck(self.model, self.original, self.work / 'models', dummy_p_width_um=args.dummy_p_width_um,
                         charge_defined_nodes=not args.charge_defined_nodes)

    def test_input_buffer_cannot_be_disconnected_from_differential_amplifier(self):
        _, inventory = self.bind()
        part = next(p for p in inventory if p['source'] == 'nmos_source_follower.hpp' and p['width_um'] == 400)
        pattern = r'(// device ' + part['device'] + r', [^\n]+\n)([^\n]+)'
        match = re.search(pattern, self.model)
        self.assertIsNotNone(match)
        changed = re.sub(r'(cpphdl_channel\(V\()n\d+', r'\g<1>n0', match[2], count=1)
        altered = self.model[:match.start(2)] + changed + self.model[match.end(2):]
        with self.assertRaisesRegex(ValueError, 'topology'):
            self.bind(model=altered)

    def test_unknown_channel_geometry_is_rejected(self):
        self.assertIn(', 0.20000000000000001,', self.model)
        with self.assertRaisesRegex(ValueError, 'buffer'):
            self.bind(model=self.model.replace(', 0.20000000000000001,', ', 0.21,', 1))

    def test_missing_storage_is_rejected(self):
        lines = self.original.splitlines()
        lines.remove(next(line for line in lines if line.startswith('Cdevice')))
        with self.assertRaisesRegex(ValueError, 'storage'):
            self.bind(deck='\n'.join(lines) + '\n')

    def test_negative_storage_is_rejected(self):
        deck = re.sub(r'(^Cdevice\d+ \S+ \S+ )\S+', r'\g<1>-1e-12', self.original, count=1, flags=re.M)
        with self.assertRaisesRegex(ValueError, 'storage'):
            self.bind(deck=deck)

    def test_last_sampler_storage_cannot_be_reduced_to_hide_charge_error(self):
        held = re.search(r'let observation37 = v\((n\d+)\)', self.original)[1]
        line = next(line for line in self.original.splitlines()
                    if line.startswith('Cdevice') and line.split()[1:3] == [held, '0']
                    and abs(float(line.split()[3]) - 1.5e-12) < 1e-24)
        changed = line.split()
        changed[3] = '1e-12'
        with self.assertRaisesRegex(ValueError, 'storage'):
            self.bind(deck=self.original.replace(line, ' '.join(changed)))

    def test_swapped_supply_observations_are_rejected(self):
        deck = self.original.replace('let observation4 =', 'let observation_tmp =')
        deck = deck.replace('let observation5 =', 'let observation4 =').replace('let observation_tmp =', 'let observation5 =')
        with self.assertRaisesRegex(ValueError, 'topology'):
            self.bind(deck=deck)

    def test_last_sampler_clock_and_held_alias_are_rejected(self):
        observations = dict(re.findall(r'let observation(\d+) = v\((n\d+)\)', self.original))
        deck = self.original.replace('let observation38 = v(' + observations['38'] + ')',
                                     'let observation38 = v(' + observations['36'] + ')')
        with self.assertRaisesRegex(ValueError, 'aliased|topology'):
            self.bind(deck=deck)
        deck = self.original.replace('let observation37 = v(' + observations['37'] + ')',
                                     'let observation37 = v(' + observations['35'] + ')')
        with self.assertRaisesRegex(ValueError, 'aliased|topology'):
            self.bind(deck=deck)

    def test_unrecognized_owner_is_rejected(self):
        with self.assertRaisesRegex(ValueError, 'inventory'):
            self.bind(model=self.model.replace('cascode_opamp.hpp:', 'unknown_amp.hpp:'))


if __name__ == '__main__':
    unittest.main(argv=[sys.argv[0], *remaining])
