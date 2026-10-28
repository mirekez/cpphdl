#!/usr/bin/env python3
"""Reject broken translator topology, unsafe voltage and incorrect output timing."""
import argparse
import copy
from pathlib import Path
import runpy
import subprocess
import sys
import tempfile
import unittest

SOURCE = Path(__file__).resolve()
sys.path.insert(0, str(SOURCE.parents[1]))
from transmit_translator_layout import circuit, realization

checker = runpy.run_path(str(SOURCE.with_name('transmit_translator_extracted.py')))


class TranslatorGuards(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.directory = tempfile.TemporaryDirectory()
        cls.work = Path(cls.directory.name)
        subprocess.run([str(EXPORTER.resolve()), str(cls.work)], check=True)
        cls.text = (cls.work / 'translator.va').read_text()

    @classmethod
    def tearDownClass(cls):
        cls.directory.cleanup()

    def source(self, text):
        path = self.work / (self.id().rsplit('.', 1)[-1] + '.va')
        path.write_text(text)
        return path

    def test_real_export(self):
        devices, storage = circuit(self.source(self.text))
        self.assertEqual((len(devices), len(storage)), (12, 19))
        self.assertEqual({d['model'] for d in devices},
                         {'sg13_lv_nmos', 'sg13_lv_pmos', 'sg13_hv_nmos', 'sg13_hv_pmos'})

    def test_explicit_psp_charge_realization(self):
        devices, storage, audit = realization(self.source(self.text), 'psp-charge')
        self.assertEqual(len(devices), 12)
        self.assertEqual({(a, b) for a, b, _ in storage}, {('Y', 'VSS'), ('Y_N', 'VSS')})
        self.assertEqual(len(audit['generic_charge_elements_replaced']), 22)
        self.assertTrue(all(abs(value - 200e-15) < 1e-25 for _, _, value in storage))

    def test_changed_storage_ownership(self):
        with self.assertRaisesRegex(ValueError, 'ownership changed'):
            realization(self.source(self.text.replace('/clock_level_shifter.hpp:',
                        '/cascoded_level_shifter.hpp:')), 'psp-charge')

    def test_unknown_realization(self):
        with self.assertRaisesRegex(ValueError, 'unreviewed'):
            realization(self.source(self.text), 'omit-capacitors')

    def test_corner_applies_to_both_device_domains(self):
        model = checker['reference'](self.source(self.text), 'psp-charge')
        for corner, temperature in (('mos_tt', 27), ('mos_ss', 125), ('mos_ff', -40)):
            deck = checker['bench'](model, 'ReferenceTranslator', Path('/models'), 2e-12, corner, temperature)
            self.assertEqual(deck.count('" ' + corner + '\n'), 2)
            self.assertIn(f'.temp {temperature}\n', deck)
            self.assertIn('cap_typ\n', deck)
        for corner, temperature in (('other', 27), ('mos_ss', 126), ('mos_ff', float('nan'))):
            with self.assertRaisesRegex(ValueError, 'unsupported translator'):
                checker['bench'](model, 'ReferenceTranslator', Path('/models'), 2e-12, corner, temperature)

    def test_unbound_device(self):
        with self.assertRaisesRegex(ValueError, 'geometry binding'):
            circuit(self.source(self.text.replace('0.02, 0.5', '0.021, 0.5', 1)))

    def test_missing_device(self):
        lines = self.text.splitlines()
        index = next(i for i, line in enumerate(lines) if '<+ cpphdl_channel' in line)
        del lines[index]
        with self.assertRaisesRegex(ValueError, 'inventory'):
            circuit(self.source('\n'.join(lines) + '\n'))

    def test_changed_interface(self):
        with self.assertRaisesRegex(ValueError, 'standalone'):
            circuit(self.source(self.text.replace('VLOGIC, BIAS', 'BIAS, VLOGIC', 1)))

    def test_negative_storage(self):
        with self.assertRaisesRegex(ValueError, 'storage branch'):
            circuit(self.source(self.text.replace('1e-14 * ddt', '-1e-14 * ddt', 1)))

    def trace(self, *, exposure=1.2, stuck=False):
        path = self.work / 'trace.dat'
        with path.open('w') as file:
            file.write('time Y Y_N A VDD VLOGIC COMMAND EXPOSURE\n')
            for step in range(20001):
                time = step * 8e-12
                phase = (time - 32e-9 - .1e-9) / 8e-9
                high = int(max(0, phase)) % 2
                output = 0 if stuck else 3.29 * high
                file.write(' '.join(map(str, [time, output, 3.29 - output, 1.2 * high,
                                             3.29, 1.2, 1.2 * high, exposure])) + '\n')
        return path

    def test_independent_electrical_limits(self):
        self.assertTrue(all(checker['measure'](self.trace())['checks'].values()))
        with self.assertRaisesRegex(ValueError, 'absolute limits'):
            checker['measure'](self.trace(stuck=True))
        with self.assertRaisesRegex(ValueError, 'absolute limits'):
            checker['measure'](self.trace(exposure=1.7))

    def test_changed_output_edges(self):
        first = checker['measure'](self.trace())
        second = copy.deepcopy(first)
        second['output_crossings'][0][1] ^= 1
        with self.assertRaisesRegex(ValueError, 'edge sequence'):
            checker['compare'](first, second)

    def test_missing_exposure_sample(self):
        path = self.trace()
        lines = path.read_text().splitlines()
        lines[100] = ' '.join(lines[100].split()[:-1])
        path.write_text('\n'.join(lines) + '\n')
        with self.assertRaisesRegex(ValueError, 'missing or nonfinite'):
            checker['measure'](path)

    def test_voltage_departure(self):
        first = checker['measure'](self.trace())
        second = copy.deepcopy(first)
        second['comparison_probes_V'] = [[v + .16 for v in row]
                                         for row in second['comparison_probes_V']]
        with self.assertRaisesRegex(ValueError, 'departure exceeds'):
            checker['compare'](first, second)

    def test_timing_departure(self):
        first = checker['measure'](self.trace())
        second = copy.deepcopy(first)
        second['output_crossings'][0][2] += 101e-12
        with self.assertRaisesRegex(ValueError, 'departure exceeds'):
            checker['compare'](first, second)


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--exporter', type=Path, required=True)
    args = parser.parse_args()
    EXPORTER = args.exporter
    unittest.main(argv=[sys.argv[0]])
