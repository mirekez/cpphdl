"""Exercise real exported ADC boundaries and reject lost or double-owned charge."""
import argparse
import copy
import json
from pathlib import Path
import re
import subprocess
import sys
import tempfile
import unittest

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))
from differential_conversion_process import process_deck
from mos_geometry import size_mos_instances
from receive_sampler_layout import circuit
from receive_sampler_integration import separate_samplers, compose
from receive_sampler_conversion import compare_codes

parser = argparse.ArgumentParser()
parser.add_argument('--bench', type=Path, required=True)
parser.add_argument('--exporter', type=Path, required=True)
args, remaining = parser.parse_known_args()


class Integration(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.temporary = tempfile.TemporaryDirectory()
        cls.root = Path(cls.temporary.name)
        standalone = cls.root / 'sampler'
        subprocess.run([args.exporter.resolve(), standalone], check=True, timeout=30)
        cls.plan = circuit(standalone / 'sampler.va', standalone / 'sampler.json')
        cls.cases = {}
        for count in (1, 4, 13):
            directory = cls.root / str(count)
            subprocess.run([args.bench.resolve(), directory, 'export', str(count), '4', '1.5', '1'],
                           check=True, timeout=30)
            metadata = json.loads((directory / 'live.json').read_text())
            deck, inventory = process_deck((directory / 'design.va').read_text(),
                (directory / 'bench.cir').read_text(), Path('/unused/models'), metadata)
            deck, _ = size_mos_instances(deck)
            cls.cases[count] = deck, inventory, metadata

    @classmethod
    def tearDownClass(cls):
        cls.temporary.cleanup()

    def separate(self, deck=None, inventory=None, count=1):
        original, entries, metadata = self.cases[count]
        return separate_samplers(original if deck is None else deck,
            entries if inventory is None else inventory, metadata, self.plan)

    def test_all_exported_bank_sizes_preserve_parent(self):
        for count in self.cases:
            with self.subTest(slices=count):
                host, bindings, audit = self.separate(count=count)
                removed = {name for s in audit['slices'] for name in s['removed']}
                expected = [line for line in self.cases[count][0].splitlines()
                            if (not line.split() or line.split()[0] not in removed)
                            and line not in audit['removed_zero_initial_conditions']]
                self.assertEqual(host.splitlines(), expected)
                self.assertEqual(audit['removed_laws'], 11 * count)
                self.assertEqual(len(bindings), count)
                self.assertIn('BhostLow', host)
                self.assertIn('Xphysical', host)  # Actual ADC/preamp devices remain.

    def test_missing_channel_rejected(self):
        deck = self.cases[1][0]
        _, _, audit = self.separate()
        name = audit['slices'][0]['removed'][0]
        with self.assertRaises(ValueError):
            self.separate(re.sub(r'^' + name + r'[^\n]*\n', '', deck, flags=re.M))

    def test_changed_dimension_and_finger_count_rejected(self):
        deck = self.cases[1][0]
        _, _, audit = self.separate()
        name = audit['slices'][0]['removed'][0]
        original, = re.findall(r'^' + name + r'[^\n]*$', deck, re.M)
        for changed in (original.replace('w=20u', 'w=21u'), original.replace('ng=2', 'ng=1')):
            self.assertNotEqual(changed, original)
            with self.assertRaises(ValueError):
                self.separate(deck.replace(original, changed))

    def test_reversed_main_channel_rejected(self):
        deck = self.cases[1][0]
        _, _, audit = self.separate()
        name = audit['slices'][0]['removed'][2]
        original, = re.findall(r'^' + name + r'[^\n]*$', deck, re.M)
        fields = original.split()
        fields[1], fields[3] = fields[3], fields[1]
        with self.assertRaises(ValueError):
            self.separate(deck.replace(original, ' '.join(fields)))

    def test_missing_duplicate_or_precharged_hold_rejected(self):
        deck = self.cases[1][0]
        _, _, audit = self.separate()
        name = audit['slices'][0]['removed'][7]
        original, = re.findall(r'^' + name + r'[^\n]*$', deck, re.M)
        for changed in ('', original + '\n' + original,
                        re.sub(r'IC=\S+', 'IC=1.65', original)):
            with self.assertRaises(ValueError):
                self.separate(deck.replace(original, changed))

    def test_private_node_load_rejected(self):
        deck = self.cases[1][0]
        _, _, audit = self.separate()
        name = audit['slices'][0]['removed'][0]
        private = re.search(r'^' + name + r' (\S+)', deck, re.M)[1]
        for card in (f'Rbad {private} 0 1e6', f'Bbad test 0 V=v({private})'):
            with self.assertRaises(ValueError):
                self.separate(deck.replace('.control', card + '\n.control'))

    def test_wrong_owner_rejected(self):
        entries = copy.deepcopy(self.cases[1][1])
        switch = next(e for e in entries if e['source'] == 'mos_track_hold.hpp')
        switch['instance'] = 'ICwrong'
        with self.assertRaises(ValueError):
            self.separate(inventory=entries)

    def test_private_cold_state_rejected(self):
        deck = self.cases[1][0]
        _, _, audit = self.separate()
        initial, = audit['removed_zero_initial_conditions']
        with self.assertRaises(ValueError):
            self.separate(deck.replace(initial, initial.replace('=0.0', '=1.65')))

    def test_adc_comparison_limits_and_missing_samples(self):
        reference = dict(codes=[128, 129], samples=[dict(target_V=1.65), dict(target_V=1.66)])
        self.assertEqual(compare_codes(reference, reference)['status'], 'pass')
        changed = copy.deepcopy(reference)
        changed['codes'][0] += 2
        self.assertEqual(compare_codes(reference, changed)['status'], 'fail')
        changed = copy.deepcopy(reference)
        changed['samples'][0]['target_V'] += .006
        self.assertEqual(compare_codes(reference, changed)['status'], 'fail')
        changed['samples'].pop()
        with self.assertRaises(ValueError):
            compare_codes(reference, changed)

    def test_composition_boundary_and_single_model(self):
        host, bindings, _ = self.separate(count=13)
        model = '.subckt ExtractedAnalog VDD VSS IN CLOCK OUT\nCtest OUT VSS 1p\n.ends\n'
        combined = compose(host, bindings, model)
        self.assertEqual(combined.count('.subckt ReceiveSamplerModel '), 1)
        self.assertEqual(len(re.findall(r'^Xreceive_sampler\d+ ', combined, re.M)), 13)
        for bad in (model.replace('IN CLOCK', 'CLOCK IN'), model + model,
                    model + '.control\nquit\n.endc\n'):
            with self.assertRaises(ValueError):
                compose(host, bindings, bad)


if __name__ == '__main__':
    unittest.main(argv=[sys.argv[0]] + remaining)
