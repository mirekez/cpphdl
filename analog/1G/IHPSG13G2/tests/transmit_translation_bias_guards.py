#!/usr/bin/env python3
"""Guard the real C++ replica binding and independent startup requirements."""
import argparse
from pathlib import Path
import runpy
import subprocess
import sys
import tempfile
import unittest

SOURCE = Path(__file__).resolve()
sys.path.insert(0, str(SOURCE.parents[1]))
from transmit_translation_bias_layout import circuit
measure = runpy.run_path(str(SOURCE.with_name('transmit_translation_bias.py')))['measure']


class ReplicaGuards(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.directory = tempfile.TemporaryDirectory()
        cls.work = Path(cls.directory.name)
        subprocess.run([str(EXPORTER.resolve()), str(cls.work)], check=True, timeout=30)
        cls.text = (cls.work / 'translation_bias.va').read_text()

    @classmethod
    def tearDownClass(cls):
        cls.directory.cleanup()

    def test_real_export_and_changed_resistance(self):
        devices, storage = circuit(self.work / 'translation_bias.va')
        self.assertEqual(len(devices), 4)
        self.assertEqual(storage, 50e-12)
        altered = self.work / 'altered.va'
        altered.write_text(self.text.replace('/ 10000.0;', '/ 9000.0;'))
        with self.assertRaisesRegex(ValueError, 'resistor topology'):
            circuit(altered)

    def test_missing_replica_and_unknown_device_binding(self):
        altered = self.work / 'altered.va'
        for text in ('\n'.join(line for line in self.text.splitlines() if '<+ cpphdl_channel' not in line) + '\n',
                     self.text.replace('0.018749999999999999', '0.019')):
            altered.write_text(text)
            with self.assertRaises(ValueError):
                circuit(altered)

    def test_changed_storage(self):
        altered = self.work / 'altered.va'
        changed = self.text.replace('5.0000000000000002e-11', '4e-11')
        self.assertNotEqual(changed, self.text)
        altered.write_text(changed)
        with self.assertRaisesRegex(ValueError, 'storage'):
            circuit(altered)

    def trace(self, fault=None):
        path = self.work / 'trace.dat'
        with path.open('w') as file:
            file.write('time BP BR VDDP VDDR LP LR ISP ISR ILP ILR\n')
            for step in range(20001):
                time = step * 100e-12
                rise = min(1, time / 1e-6)
                row = [time, 1.8 * rise, 1.82 * rise, 3.29, 3.29, 1.19, 1.19,
                       -.0002, -.0002, -.0012, -.0012]
                if fault == 'headroom':
                    row[1] = 1.1 * rise
                elif fault == 'supply':
                    row[5] = 1.07
                elif fault == 'settling' and time >= 1.6e-6:
                    row[1] += .005 * (step % 2)
                elif fault == 'comparison':
                    row[1] = 1.95 * rise
                elif fault == 'nonfinite' and step == 100:
                    row[1] = float('nan')
                elif fault == 'missing' and step == 20000:
                    continue
                elif fault == 'missing_start' and step < 100:
                    continue
                file.write(' '.join(map(str, row)) + '\n')
        return path

    def test_startup_limits_and_comparison(self):
        result = measure(self.trace())
        self.assertLess(result['comparison']['maximum_settled_relative_error'], .05)
        for fault in ('headroom', 'supply', 'settling', 'comparison', 'nonfinite', 'missing', 'missing_start'):
            with self.subTest(fault=fault):
                with self.assertRaises(ValueError):
                    measure(self.trace(fault))


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--exporter', type=Path, required=True)
    EXPORTER = parser.parse_args().exporter
    unittest.main(argv=[sys.argv[0]])
