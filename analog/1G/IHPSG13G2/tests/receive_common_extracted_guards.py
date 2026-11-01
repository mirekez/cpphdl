#!/usr/bin/env python3
"""Exercise IC boundaries and comparison failures using actual exported benches."""
import argparse
import json
from pathlib import Path
import runpy
import subprocess
import sys
import tempfile
import unittest

SOURCE = Path(__file__).resolve()
sys.path[:0] = [str(SOURCE.parents[1]), str(SOURCE.parents[3] / 'pdk/1G_repeater_IHPSG13G2')]
from receive_common_extracted import separate_host, compose
from lead_receive_common_process import process_deck

parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument('--exporter', type=Path, required=True)
parser.add_argument('--bench', type=Path, required=True)
parser.add_argument('--pdk', type=Path, required=True)
args, remaining = parser.parse_known_args()
checker = runpy.run_path(str(SOURCE.with_name('receive_common_extracted.py')))


class ExtractedGuards(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.temporary = tempfile.TemporaryDirectory(prefix='receive-extracted-guards-')
        cls.work = Path(cls.temporary.name)
        for executable, name, extra in ((args.exporter, 'reference', []), (args.bench, 'bench', ['export'])):
            subprocess.run([executable.resolve(), cls.work / name, *extra], check=True, timeout=30)
        cls.source, cls.probes = cls.work / 'reference/reference.va', cls.work / 'reference/probes.json'
        model = (cls.work / 'bench/design.va').read_text()
        deck = (cls.work / 'bench/bench.cir').read_text()
        cls.mapped, cls.inventory = process_deck(model, deck,
            args.pdk.resolve() / 'ihp-sg13g2/libs.tech/ngspice/models')

    @classmethod
    def tearDownClass(cls):
        cls.temporary.cleanup()

    def separate(self, deck=None, inventory=None):
        return separate_host(self.mapped if deck is None else deck,
            self.inventory if inventory is None else inventory, self.source, self.probes)

    def test_actual_host_and_observations(self):
        host, mapping, audit = self.separate()
        self.assertEqual(audit, dict(removed_laws=39, host_mos=0, host_resistors=57, host_capacitors=18))
        model = '.subckt ReceiveReference VDD VSS OUT\n.ends ReceiveReference\n'
        result = compose(host, mapping, model, {n: n for n in mapping})
        self.assertIn('let observation3 = v(Xreference.DRIVE)', result)
        for index in range(7, 39):
            before = next(line for line in host.splitlines() if line.startswith(f'let observation{index} ='))
            self.assertIn(before, result)
        self.assertEqual(result.count('Xreference '), 1)

    def test_private_node_load_rejected(self):
        _, mapping, _ = self.separate()
        changed = self.mapped.replace('.control', f'Rhidden {mapping["DRIVE"]} 0 1000\n.control')
        with self.assertRaisesRegex(ValueError, 'private node'):
            self.separate(changed)

    def test_compensation_alias_rejected(self):
        _, mapping, _ = self.separate()
        lead = next(value for key, value in mapping.items() if key.startswith('N'))
        import re
        changed = re.sub(r'\b' + lead + r'\b', mapping['TARGET'], self.mapped)
        with self.assertRaisesRegex(ValueError, 'compensation node is aliased'):
            self.separate(changed)

    def test_wrong_width_rejected(self):
        inventory = json.loads(json.dumps(self.inventory))
        inventory['mos'][-1]['width_um'] += 5
        with self.assertRaisesRegex(ValueError, 'transistor binding'):
            self.separate(inventory=inventory)

    def test_missing_capacitor_rejected(self):
        line = next(line for line in self.mapped.splitlines()
                    if line.startswith('Cdevice') and float(line.split()[3]) == 80e-12)
        with self.assertRaisesRegex(ValueError, 'reference capacitor'):
            self.separate(self.mapped.replace(line + '\n', ''))

    def test_external_output_and_last_lane_have_same_budget(self):
        rows = [[16e-6 + i * 2e-12] + [1.8] * 39 for i in range(100)]
        self.assertEqual(checker['compare_flows'](rows, rows)['status'], 'pass')
        for column in (1, 39):
            changed = [list(r) for r in rows]
            changed[50][column] += .006
            result = checker['compare_flows'](rows, changed)
            self.assertEqual(result['status'], 'fail')
            self.assertFalse(result['checks'][checker['HEADER'][column]])

    def test_failed_geometry_rejected_before_use(self):
        path = self.work / 'bad-layout.json'
        path.write_text(json.dumps(dict(status='fail')))
        with self.assertRaisesRegex(ValueError, 'geometry'):
            checker['layout_evidence'](path)

    def test_conflicting_provenance_rejected(self):
        with self.assertRaisesRegex(ValueError, 'provenance'):
            checker['merge']({'one': 'before'}, {'one': 'after'})


if __name__ == '__main__':
    unittest.main(argv=[sys.argv[0]] + remaining)
