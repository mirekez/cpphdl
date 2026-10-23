"""Reject altered host controls, storage and feedback in closed conversions."""
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
from programmable_conversion_process import process_deck

parser = argparse.ArgumentParser()
parser.add_argument('--bench', type=Path, required=True)
args, remaining = parser.parse_known_args()


class Bindings(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.temporary = tempfile.TemporaryDirectory(prefix='cpphdl-programmable-conversion-bindings-')
        cls.work = Path(cls.temporary.name)
        cls.examples = {}
        for count in (1, 4, 16):
            case = cls.work / str(count)
            subprocess.run([args.bench.resolve(), case, 'export', str(count), '4', '1', '2' if count == 16 else '1'],
                           check=True, timeout=30)
            cls.examples[count] = ((case / 'design.va').read_text(), (case / 'bench.cir').read_text(),
                                   json.loads((case / 'live.json').read_text()))

    @classmethod
    def tearDownClass(cls):
        cls.temporary.cleanup()

    def bind(self, count=1, model=None, deck=None, metadata=None):
        original_model, original_deck, original_metadata = self.examples[count]
        return process_deck(original_model if model is None else model, original_deck if deck is None else deck,
                            self.work / 'models', original_metadata if metadata is None else metadata)

    def test_all_array_sizes_preserve_reviewed_devices_and_both_host_rail_currents(self):
        for count in (1, 4, 16):
            with self.subTest(slices=count):
                deck, inventory = self.bind(count)
                self.assertEqual(len(inventory), 26 + 59 * count)
                self.assertEqual(deck.count('BhostLow'), 9 * count)
                self.assertEqual(deck.count('BhostHigh'), 9 * count)
                self.assertEqual(len(re.findall(r'w=25u l=0.45u ng=5$', deck, re.M)), 2 * count)
                self.assertNotIn('cpphdl_channel', deck)
                self.assertNotIn('CircuitModel', deck)
                self.assertEqual(len(re.findall(r'^let observation', deck, re.M)), 7 + 3 * count)

    def test_changed_storage_is_not_accepted_by_a_different_exported_profile(self):
        _, deck, metadata = self.examples[1]
        held = re.search(r'let observation7 = v\((n\d+)\)', deck)[1]
        line = next(line for line in deck.splitlines() if line.startswith('Cdevice')
                    and line.split()[1:3] == [held, '0'] and abs(float(line.split()[3]) - 1e-12) < 1e-24)
        fields = line.split()
        fields[3] = '1.5e-12'
        with self.assertRaisesRegex(ValueError, 'storage'):
            self.bind(deck=deck.replace(line, ' '.join(fields)))

    def test_host_port_order_and_identity_cannot_change(self):
        for fault in ('order', 'identity'):
            metadata = copy.deepcopy(self.examples[1][2])
            if fault == 'order':
                metadata['drivers'][0], metadata['drivers'][1] = metadata['drivers'][1], metadata['drivers'][0]
            else:
                metadata['drivers'][1]['id'] = metadata['drivers'][0]['id']
            with self.subTest(fault=fault), self.assertRaisesRegex(ValueError, 'controller'):
                self.bind(metadata=metadata)

    def test_feedback_must_come_from_the_actual_core_voltage_translator(self):
        _, deck, metadata = self.examples[1]
        metadata = copy.deepcopy(metadata)
        held = re.search(r'let observation7 = v\((n\d+)\)', deck)[1]
        old = metadata['decisions'][0]
        metadata['decisions'][0] = held
        deck = deck.replace('let observation9 = v(' + old + ')', 'let observation9 = v(' + held + ')')
        with self.assertRaisesRegex(ValueError, 'core interfaces'):
            self.bind(deck=deck, metadata=metadata)

    def test_host_only_driver_is_forbidden_inside_an_ic(self):
        model = self.examples[1][0]
        match = re.search(r'(?m)^(\s*// device \d+, )[^\n]+[.](?:cpp|hpp):\d+\n(?=\s*I[^\n]+// sim_driver)', model)
        self.assertIsNotNone(match)
        altered = model[:match.start()] + match[1] + 'IC forbidden, /basic/inverter.hpp:1\n' + model[match.end():]
        with self.assertRaisesRegex(ValueError, 'host'):
            self.bind(model=altered)

    def test_missing_or_duplicated_host_branch_is_rejected(self):
        model = self.examples[1][0]
        line = next(line for line in model.splitlines() if '// sim_driver' in line)
        for replacement in ('', line + '\n' + line):
            with self.subTest(replacement=replacement), self.assertRaisesRegex(ValueError, 'driver'):
                self.bind(model=model.replace(line, replacement, 1))

    def test_host_current_must_return_to_the_declared_rails(self):
        model, deck, _ = self.examples[1]
        vdd = re.search(r'let observation4 = v\((n\d+)\)', deck)[1]
        line = next(line for line in model.splitlines() if '// sim_driver' in line)
        with self.assertRaisesRegex(ValueError, 'driver'):
            self.bind(model=model.replace(line, line.replace('n0', vdd), 1))


if __name__ == '__main__':
    unittest.main(argv=[sys.argv[0], *remaining])
