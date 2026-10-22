"""Reject physical bindings that disagree with the actual exported gain IC."""
import argparse
import csv
import io
from pathlib import Path
import subprocess
import sys
import tempfile
import unittest

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))
from receive_gain_process import process_deck

parser = argparse.ArgumentParser()
parser.add_argument('--bench', type=Path, required=True)
args, remaining = parser.parse_known_args()


class Bindings(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.temporary = tempfile.TemporaryDirectory(prefix='cpphdl-gain-bindings-')
        cls.work = Path(cls.temporary.name)
        subprocess.run([args.bench.resolve(), cls.work, 'export', '4', '20'], check=True, timeout=30)
        cls.model = (cls.work / 'design.va').read_text()
        cls.original = (cls.work / 'bench.cir').read_text()
        cls.inventory = (cls.work / 'process_devices.csv').read_text()

    @classmethod
    def tearDownClass(cls):
        cls.temporary.cleanup()

    def bind(self, inventory=None, model=None, original=None):
        return process_deck(model or self.model, original or self.original,
                            inventory or self.inventory, Path('/reviewed-models'))

    def changed_inventory(self, change):
        reader = csv.DictReader(io.StringIO(self.inventory))
        rows, fields = list(reader), reader.fieldnames
        change(rows)
        text = io.StringIO()
        writer = csv.DictWriter(text, fields, lineterminator='\n')
        writer.writeheader()
        writer.writerows(rows)
        return text.getvalue()

    def test_real_elaborated_module_lowers_completely(self):
        deck, report = self.bind()
        self.assertEqual(report['mos_count'], 20)
        self.assertNotIn('CircuitModel', deck)
        self.assertEqual(sum(line.startswith('Xphysical') for line in deck.splitlines()), 20)
        self.assertEqual(sum(line.startswith('Rphysical') for line in deck.splitlines()), 24)

    def test_pmos_body_cannot_be_grounded(self):
        def change(rows):
            next(row for row in rows if row['kind'] == 'P')['body'] = '0'
        with self.assertRaisesRegex(ValueError, 'body'):
            self.bind(inventory=self.changed_inventory(change))

    def test_swapped_sizes_cannot_hide_in_the_correct_inventory(self):
        def change(rows):
            a = next(row for row in rows if row['kind'] == 'N' and row['width_um'] == '100')
            b = next(row for row in rows if row['kind'] == 'N' and row['width_um'] == '200')
            a['width_um'], b['width_um'] = b['width_um'], a['width_um']
        with self.assertRaisesRegex(ValueError, 'MOS differs'):
            self.bind(inventory=self.changed_inventory(change))

    def test_changed_gate_cannot_rewire_the_export(self):
        def change(rows):
            next(row for row in rows if row['kind'] == 'N')['gate'] = '0'
        with self.assertRaisesRegex(ValueError, 'MOS differs'):
            self.bind(inventory=self.changed_inventory(change))

    def test_missing_storage_is_rejected(self):
        lines = self.original.splitlines()
        index = next(i for i, line in enumerate(lines) if line.startswith('Cdevice'))
        lines.pop(index)
        with self.assertRaisesRegex(ValueError, 'storage'):
            self.bind(original='\n'.join(lines) + '\n')

    def test_unknown_current_law_is_rejected(self):
        model = self.model.replace('    end\nendmodule', '        I(n1, n0) <+ sin(V(n1, n0));\n    end\nendmodule')
        with self.assertRaises(ValueError):
            self.bind(model=model)


if __name__ == '__main__':
    unittest.main(argv=[sys.argv[0], *remaining])
