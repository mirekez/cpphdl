"""Check charge ownership against actual exported C++ and qualified MOS banks."""
import argparse
import copy
import json
from pathlib import Path
import subprocess
import sys
import tempfile
import unittest

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))
from input_charge import verify_files
from transmit_charge_realization import charge_plan
from transmit_storage_layout import storage_partition

parser = argparse.ArgumentParser()
parser.add_argument('--exporter', type=Path, required=True)
parser.add_argument('--proof', type=Path, required=True)
args, remaining = parser.parse_known_args()


class ChargeOwnership(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        proof = json.loads(args.proof.read_text())
        if proof.get('status') != 'pass':
            raise ValueError('passing physical current-cell fixture required')
        verify_files(proof['source_sha256'])
        for case in proof['cases'].values():
            if case['local_drc_violations'] or not case['cut_gate_rejected']:
                raise ValueError('missing current-cell geometry/fault evidence')
            verify_files(case['artifact_sha256'])
            verify_files(case['extraction']['source_sha256'])
            verify_files(case['extraction']['artifact_sha256'])
        cls.geometry = {name: proof['cases'][name]['geometry'] for name in ('main', 'history')}
        cls.temporary = tempfile.TemporaryDirectory(prefix='cpphdl-tx-charge-')
        cls.addClassCleanup(cls.temporary.cleanup)
        cls.export = Path(cls.temporary.name) / 'export'
        subprocess.run([str(args.exporter.resolve()), str(cls.export)], check=True, timeout=30)

    def test_all_intentions_owned_once(self):
        plan = charge_plan(self.export, self.geometry)
        self.assertEqual(plan['status'], 'candidate_unqualified')
        self.assertEqual(plan['retained_capacitors'], 20)
        self.assertEqual(plan['modeled_charge_contributions'], 64)
        records = plan['retained_storage'] + plan['modeled_charge']
        self.assertEqual(len({r['element'] for r in records}), 84)
        for group in plan['groups'].values():
            self.assertEqual(len(group['capacitors']), 2)
            self.assertEqual(len(group['modeled_charge']), 8)

    def test_shared_and_local_compensation_preserved(self):
        before = storage_partition(self.export)
        plan = charge_plan(self.export, self.geometry)
        kept = plan['retained_storage']
        self.assertEqual(kept[:4], before['groups']['shared']['capacitors'])
        for name, group in plan['groups'].items():
            self.assertEqual(group['capacitors'], before['groups'][name]['capacitors'][:2])
        self.assertEqual(storage_partition(self.export), before)

    def test_wrong_physical_gate_or_source_rejected(self):
        for bank in ('main', 'history'):
            for terminal in ('G', 'S'):
                geometry = copy.deepcopy(self.geometry)
                geometry[bank]['devices'][0]['nets'][terminal] = 'VSS'
                with self.assertRaises(ValueError):
                    charge_plan(self.export, geometry)

    def test_changed_process_geometry_rejected(self):
        geometry = copy.deepcopy(self.geometry)
        geometry['main']['devices'][0]['geometry']['total_width_um'] *= 2
        with self.assertRaises(ValueError):
            charge_plan(self.export, geometry)

    def test_no_mos_role_loses_charge_assignment(self):
        plan = charge_plan(self.export, self.geometry)
        for group in plan['groups'].values():
            for index in range(4):
                assignments = [r for r in group['modeled_charge'] if r['device_index'] == index]
                self.assertEqual({tuple(r['model_terminals']) for r in assignments}, {('G', 'S'), ('G', 'D')})
                self.assertTrue(all(r['model'] == 'sg13_hv_nmos' for r in assignments))


if __name__ == '__main__':
    unittest.main(argv=[sys.argv[0]] + remaining)
