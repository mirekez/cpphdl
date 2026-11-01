#!/usr/bin/env python3
"""Reject incorrect receive-reference bindings before physical generation."""
import argparse
from collections import Counter
import json
from pathlib import Path
import re
import subprocess
import sys
import tempfile
import unittest

SOURCE = Path(__file__).resolve()
sys.path.insert(0, str(SOURCE.parents[1]))
from receive_common_layout import circuit
from lead_receive_common_process import process_deck

parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument('--exporter', type=Path, required=True)
parser.add_argument('--bench', type=Path, required=True)
parser.add_argument('--pdk', type=Path, required=True)
args, remaining = parser.parse_known_args()


class ReceiveLayoutGuards(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.temporary = tempfile.TemporaryDirectory(prefix='receive-layout-guards-')
        cls.work = Path(cls.temporary.name)
        subprocess.run([args.exporter.resolve(), cls.work / 'export'], check=True, timeout=30)
        subprocess.run([args.bench.resolve(), cls.work / 'bench', 'export'], check=True, timeout=30)
        cls.original = (cls.work / 'export/reference.va').read_text()
        cls.probes = json.loads((cls.work / 'export/probes.json').read_text())

    @classmethod
    def tearDownClass(cls):
        cls.temporary.cleanup()

    def parse(self, text=None, probes=None):
        source, names = self.work / 'candidate.va', self.work / 'candidate.json'
        source.write_text(self.original if text is None else text)
        names.write_text(json.dumps(self.probes if probes is None else probes))
        return circuit(source, names)

    def test_actual_ic_matches_independent_process_binding(self):
        devices, resistors, capacitors = self.parse()
        self.assertEqual((len(devices), len(resistors), len(capacitors)), (11, 9, 19))
        _, reference = process_deck((self.work / 'bench/design.va').read_text(),
            (self.work / 'bench/bench.cir').read_text(),
            args.pdk.resolve() / 'ihp-sg13g2/libs.tech/ngspice/models')
        signature = lambda d: (d['model'], d['width_um'], d['length_um'])
        self.assertEqual(Counter(map(signature, devices)), Counter(map(signature, reference['mos'])))
        self.assertAlmostEqual(sum(c[2] for c in capacitors) / 1e-12, 122.69)

    def test_aliased_internal_probe(self):
        probes = dict(self.probes, TARGET=self.probes['DRIVE'])
        with self.assertRaises(ValueError):
            self.parse(probes=probes)

    def test_feedback_reversed(self):
        a, b = self.probes['TARGET'], 'OUT'
        pattern = re.compile(r'\b(' + a + '|' + b + r')\b')
        # Reverse only the two input gates; device terminals/counts stay valid.
        lines = self.original.splitlines()
        for index, line in enumerate(lines):
            if 'cpphdl_channel(V(' in line and ('V(' + a + ',' in line or 'V(OUT,' in line):
                lines[index] = pattern.sub(lambda m: b if m[0] == a else a, line)
        changed = '\n'.join(lines) + '\n'
        self.assertNotEqual(changed, self.original)
        with self.assertRaisesRegex(ValueError, 'feedback or device'):
            self.parse(changed)

    def test_target_ratio_changed(self):
        self.assertIn('/ 12150.0;', self.original)
        with self.assertRaisesRegex(ValueError, 'bias or compensation'):
            self.parse(self.original.replace('/ 12150.0;', '/ 12000.0;', 1))

    def test_output_storage_wrong_return(self):
        line = next(line for line in self.original.splitlines()
                    if 'I(OUT, VSS)' in line and 'ddt' in line)
        changed = line.replace('VSS', self.probes['TARGET'])
        with self.assertRaisesRegex(ValueError, 'bias or compensation'):
            self.parse(self.original.replace(line, changed))

    def test_nonzero_initial_charge(self):
        with self.assertRaisesRegex(ValueError, 'unbound'):
            self.parse(self.original.replace('initial voltage = 0.0 V.', 'initial voltage = 0.1 V.', 1))

    def test_missing_sink(self):
        line = next(line for line in self.original.splitlines()
                    if 'I(OUT, VSS)' in line and 'cpphdl_channel' in line)
        with self.assertRaisesRegex(ValueError, 'inventory'):
            self.parse(self.original.replace(line + '\n', ''))

    def test_wrong_device_owner(self):
        changed = re.sub(r'([^\n]*/)(shunt_voltage_buffer.hpp)(:\d+\n[^\n]*cpphdl_channel)',
                         r'\1cascode_ota.hpp\3', self.original)
        self.assertNotEqual(changed, self.original)
        with self.assertRaisesRegex(ValueError, 'feedback or device'):
            self.parse(changed)

    def test_extra_unowned_law(self):
        with self.assertRaisesRegex(ValueError, 'inventory'):
            self.parse(self.original.replace('    end\nendmodule',
                '        I(OUT, VSS) <+ 1e-3;\n    end\nendmodule'))


if __name__ == '__main__':
    unittest.main(argv=[sys.argv[0]] + remaining)
