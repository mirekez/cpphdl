#!/usr/bin/env python3
"""Reject incorrect bank topology and incomplete observations; check the probe in SPICE."""
import argparse
import gzip
import json
from pathlib import Path
import runpy
import shutil
import subprocess
import sys
import tempfile
import unittest

SOURCE = Path(__file__).resolve()
sys.path.insert(0, str(SOURCE.parents[1]))
from transmit_translation_bank import validate_export, with_exposure_probe

parser = argparse.ArgumentParser()
parser.add_argument('--exporter', type=Path, required=True)
parser.add_argument('--ngspice', type=Path, required=True)
args, remaining = parser.parse_known_args()
measure = runpy.run_path(str(SOURCE.with_name('transmit_translation_bank.py')))['measure']


class BankGuards(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.temporary = tempfile.TemporaryDirectory(prefix='cpphdl-bank-guards-')
        cls.work = Path(cls.temporary.name)
        subprocess.run([args.exporter.resolve(), cls.work / 'original'], check=True, timeout=30)
        cls.original = cls.work / 'original'
        cls.valid = cls.work / 'valid.dat.gz'
        with gzip.open(cls.valid, 'wt') as stream:
            stream.write('time ' + ' '.join('n' + str(i) for i in range(92)) + '\n')
            for index in range(68097):
                time = index * 31.25e-12
                high = int(max(0, (time - 2e-6 - .1e-9) / 8e-9)) % 2
                row = [time, 3.29, 1.2, min(1.8, time / 1e-6 * 1.8), 1.2 * high]
                row += [3.29 * high, 3.29 * (1 - high), 1.2 * high, 1.2] * 22
                stream.write(' '.join(map(str, row)) + '\n')

    @classmethod
    def tearDownClass(cls):
        cls.temporary.cleanup()

    def setUp(self):
        self.export = self.work / self._testMethodName
        shutil.copytree(self.original, self.export)

    def validate(self):
        return validate_export(self.export, self.original / 'translator_0.va',
                               self.original / 'translation_bias.va')

    def test_actual_22_translators(self):
        self.assertEqual(len(self.validate()['translators']), 22)

    def test_unshared_replica(self):
        path = self.export / 'bank.json'
        value = json.loads(path.read_text())
        value['translators'][21]['bias_node'] += 1
        path.write_text(json.dumps(value))
        with self.assertRaisesRegex(ValueError, 'sharing'):
            self.validate()

    def test_aliased_outputs(self):
        path = self.export / 'bank.json'
        value = json.loads(path.read_text())
        value['translators'][21]['output_node'] = value['translators'][0]['output_node']
        path.write_text(json.dumps(value))
        with self.assertRaisesRegex(ValueError, 'sharing'):
            self.validate()

    def test_changed_actual_translator(self):
        path = self.export / 'translator_21.va'
        text = path.read_text()
        marker = '0.059999999999999998'
        self.assertIn(marker, text)
        path.write_text(text.replace(marker, '0.061', 1))
        with self.assertRaises(ValueError):
            self.validate()

    def test_every_output_observed(self):
        result = measure(self.valid)
        self.assertEqual(result['status'], 'pass')
        self.assertEqual(len(result['gates']), 22)
        changed = self.work / 'unsafe.dat.gz'
        with gzip.open(self.valid, 'rt') as source, gzip.open(changed, 'wt') as destination:
            for index, line in enumerate(source):
                if index == 100:
                    fields = line.split()
                    fields[-1] = '1.7'  # Only the final IC is unsafe during startup.
                    line = ' '.join(fields) + '\n'
                destination.write(line)
        result = measure(changed)
        self.assertEqual(result['status'], 'fail')
        self.assertFalse(result['gates'][21]['checks']['lv_gate_exposure'])

    def test_missing_startup(self):
        changed = self.work / 'missing-start.dat.gz'
        with gzip.open(self.valid, 'rt') as source, gzip.open(changed, 'wt') as destination:
            for index, line in enumerate(source):
                if index == 0 or index > 100:
                    destination.write(line)
        with self.assertRaisesRegex(ValueError, 'startup'):
            measure(changed)

    def test_spice_probe_measures_all_terminal_differences(self):
        model = '.subckt ProbeDUT D G S B\nXmos D G S B sg13_lv_nmos\n.ends ProbeDUT\n'
        model = with_exposure_probe(model, 'ProbeDUT')
        # The isolated dummy model makes the expected signed differences exact.
        # It tests observation syntax/semantics, not the foundry transistor law.
        text = ('Probe test\n' + model +
            '.subckt sg13_lv_nmos D G S B\nRdummy D S 1e12\n.ends sg13_lv_nmos\n'
            'Xdut D G S B ProbeDUT\nVD D 0 .2\nVS S 0 .6\nVB B 0 1.4\n'
            'VG G 0 PWL(0 -.1 1n -.1 2n 1.7 3n 1.7)\n'
            '.control\nset wr_singlescale\nset wr_vecnames\nset numdgt=17\n'
            'tran 10p 3n\nwrdata trace.dat v(G) v(Xdut.bank_lv_exposure)\nquit\n.endc\n.end\n')
        (self.export / 'probe.cir').write_text(text)
        with (self.export / 'probe.log').open('w') as log:
            subprocess.run([args.ngspice.resolve(), '-n', '-b', 'probe.cir'], cwd=self.export,
                           stdout=log, stderr=-2, check=True, timeout=30)
        rows = [[float(v) for v in line.split()] for line in
                (self.export / 'trace.dat').read_text().splitlines()[1:]]
        self.assertGreater(len(rows), 100)
        for _, gate, observed in rows:
            self.assertAlmostEqual(observed, max(abs(gate - v) for v in (.2, .6, 1.4)), places=12)


if __name__ == '__main__':
    unittest.main(argv=[sys.argv[0]] + remaining)
