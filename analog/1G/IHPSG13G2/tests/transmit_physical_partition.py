#!/usr/bin/env python3
"""Reject lost TX storage, duplicated laws and incorrect physical block wiring."""
import argparse
import json
from pathlib import Path
import shutil
import subprocess
import sys
import tempfile
import unittest

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))
from transmit_physical_partition import validate_partition

parser = argparse.ArgumentParser()
parser.add_argument('--exporter', type=Path, required=True)
args, remaining = parser.parse_known_args()


class TransmitPartition(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.original = tempfile.TemporaryDirectory(prefix='cpphdl-tx-partition-')
        cls.source = Path(cls.original.name) / 'export'
        subprocess.run([str(args.exporter.resolve()), str(cls.source)], check=True, timeout=30)

    @classmethod
    def tearDownClass(cls):
        cls.original.cleanup()

    def setUp(self):
        self.temporary = tempfile.TemporaryDirectory(prefix='cpphdl-tx-partition-fault-')
        self.work = Path(self.temporary.name) / 'export'
        shutil.copytree(self.source, self.work)
        self.manifest = json.loads((self.work / 'partition.json').read_text())

    def tearDown(self):
        self.temporary.cleanup()

    def check(self):
        (self.work / 'partition.json').write_text(json.dumps(self.manifest, indent=2) + '\n')
        return validate_partition(self.work)

    def test_whole_cpp_transmitter_is_covered(self):
        result = self.check()
        self.assertEqual(result['elements'], 931)
        self.assertEqual(result['current_cells'], 8)
        self.assertEqual(result['remaining_capacitors'], 84)
        self.assertEqual(result['internal_storage_anchors'], 16)

    def test_missing_block_rejected(self):
        del self.manifest['blocks'][25]
        with self.assertRaisesRegex(ValueError, 'block inventory'):
            self.check()

    def test_duplicate_contribution_rejected(self):
        indices = self.manifest['blocks'][-1]['elements']
        indices[-1] = indices[-2]
        with self.assertRaisesRegex(ValueError, 'aliased or incomplete'):
            self.check()

    def test_bit_command_swap_rejected(self):
        first, second = self.manifest['blocks'][:2]
        first['ports']['A'], second['ports']['A'] = second['ports']['A'], first['ports']['A']
        with self.assertRaisesRegex(ValueError, 'changes a law or connection'):
            self.check()

    def test_internal_storage_anchor_swap_rejected(self):
        ports = self.manifest['blocks'][24]['ports']
        ports['TAIL'], ports['LOWER'] = ports['LOWER'], ports['TAIL']
        with self.assertRaisesRegex(ValueError, 'changes a law or connection'):
            self.check()

    def test_missing_capacitor_rejected(self):
        path = self.work / 'remaining_passives.va'
        lines = path.read_text().splitlines()
        target = next(line for line in lines if 'I(' in line and 'ddt(' in line)
        path.write_text('\n'.join(line for line in lines if line != target) + '\n')
        with self.assertRaisesRegex(ValueError, 'contribution inventory'):
            self.check()

    def test_modified_storage_value_rejected(self):
        path = self.work / 'remaining_passives.va'
        text = path.read_text()
        self.assertIn('4.0000000000000001e-10', text)
        path.write_text(text.replace('4.0000000000000001e-10', '2.0000000000000001e-10', 1))
        with self.assertRaisesRegex(ValueError, 'changes a law or connection'):
            self.check()

    def test_undeclared_extra_law_rejected(self):
        path = self.work / 'main_0.va'
        text = path.read_text().replace('    analog begin\n', '    analog begin\n        I(P, VSS) <+ 1.0;\n')
        path.write_text(text)
        with self.assertRaisesRegex(ValueError, 'contribution inventory'):
            self.check()


if __name__ == '__main__':
    unittest.main(argv=[sys.argv[0]] + remaining)
