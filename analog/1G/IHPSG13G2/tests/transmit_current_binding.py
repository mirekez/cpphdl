#!/usr/bin/env python3
"""Check real C++ current-cell bindings and preserve extracted storage anchors."""
import argparse
import copy
import hashlib
import json
from pathlib import Path
import re
import subprocess
import sys
import tempfile
import unittest

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))
from transmit_current_binding import bind_current_cell, expose_storage_anchors, PORTS

parser = argparse.ArgumentParser()
parser.add_argument('--exporter', type=Path, required=True)
parser.add_argument('--proof', type=Path, required=True)
args, remaining = parser.parse_known_args()


class CurrentBinding(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.proof = json.loads(args.proof.read_text())
        if cls.proof.get('status') != 'pass':
            raise ValueError('passing current-cell proof required')
        evidence = [cls.proof['source_sha256']]
        for case in cls.proof['cases'].values():
            if case.get('local_drc_violations') != 0 or case.get('cut_gate_rejected') is not True:
                raise ValueError('missing current-cell geometry/fault-rejection evidence')
            evidence += [case['artifact_sha256'], case['extraction']['source_sha256'],
                         case['extraction']['artifact_sha256']]
        for group in evidence:
            for path, value in group.items():
                if hashlib.sha256(Path(path).read_bytes()).hexdigest() != value:
                    raise ValueError('changed current-cell fixture: ' + path)
        cls.temporary = tempfile.TemporaryDirectory(prefix='cpphdl-tx-current-binding-')
        cls.export = Path(cls.temporary.name) / 'export'
        subprocess.run([str(args.exporter.resolve()), str(cls.export)], check=True, timeout=30)
        cls.network = {name: (Path(cls.proof['work']) / name / 'extraction/conserved.spice').read_text()
                       for name in ('main', 'history')}

    @classmethod
    def tearDownClass(cls):
        cls.temporary.cleanup()

    def test_all_actual_cells_match_physical_banks(self):
        for bank in ('main', 'history'):
            geometry = self.proof['cases'][bank]['geometry']
            for bit in range(4):
                with self.subTest(bank=bank, bit=bit):
                    devices = bind_current_cell(self.export / f'{bank}_{bit}.va', geometry, bank == 'history')
                    self.assertEqual(len(devices), 4)

    def test_anchor_ports_preserve_every_internal_equation(self):
        for name, text in self.network.items():
            result = expose_storage_anchors(text, 'BoundCell')
            self.assertIn('.subckt BoundCell ' + ' '.join(PORTS), result)
            body = lambda s: [line for line in s.splitlines()
                              if not line.lower().startswith(('.subckt', '.ends'))]
            self.assertEqual(body(text), body(result))

    def test_wrong_steering_connection_rejected(self):
        geometry = copy.deepcopy(self.proof['cases']['main']['geometry'])
        geometry['devices'][0]['nets']['G'] = 'GN'
        with self.assertRaisesRegex(ValueError, 'steering connection'):
            bind_current_cell(self.export / 'main_0.va', geometry)

    def test_changed_physical_width_rejected(self):
        geometry = copy.deepcopy(self.proof['cases']['main']['geometry'])
        geometry['devices'][0]['geometry']['total_width_um'] = 200
        with self.assertRaisesRegex(ValueError, 'physical current-cell dimensions'):
            bind_current_cell(self.export / 'main_0.va', geometry)

    def test_missing_anchor_wire_rejected_even_with_remaining_capacitance(self):
        text = self.network['main']
        removed = [line for line in text.splitlines() if line.startswith('R') and 'TAIL' in line.split()[1:3]]
        self.assertTrue(removed)
        changed = '\n'.join(line for line in text.splitlines() if line not in removed) + '\n'
        self.assertTrue(any(line.startswith('C') and 'TAIL' in line.split()[1:3] for line in changed.splitlines()))
        with self.assertRaisesRegex(ValueError, 'physically connected'):
            expose_storage_anchors(changed, 'BoundCell')

    def test_changed_original_interface_and_nonfinite_wire_rejected(self):
        text = self.network['main']
        with self.assertRaisesRegex(ValueError, 'original extracted current-cell ports'):
            expose_storage_anchors(text.replace(' GP GN ', ' GN GP ', 1), 'BoundCell')
        changed = re.sub(r'^(R\S+ \S+ \S+) \S+$', r'\1 nan', text, count=1, flags=re.M)
        self.assertNotEqual(changed, text)
        with self.assertRaisesRegex(ValueError, 'invalid current-cell extracted wire'):
            expose_storage_anchors(changed, 'BoundCell')


if __name__ == '__main__':
    unittest.main(argv=[sys.argv[0]] + remaining)
