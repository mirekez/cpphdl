#!/usr/bin/env python3
"""Fault injection for separate frontend, sampler, hold and code contracts."""
import csv
import json
from pathlib import Path
import tempfile
import unittest

from receive_conversion import check


class ReceiveConversionContract(unittest.TestCase):
    def experiment(self, directory, fault=None):
        work = Path(directory)
        (work / 'cpp').mkdir()
        header = ['time', 'PLUS', 'MINUS', 'DRAIN', 'INPUT', 'VDD', 'VLOGIC', 'HELD0', 'CORE_DECISION0']
        with (work / 'cpp/trace.csv').open('w') as stream:
            writer = csv.writer(stream)
            writer.writerow(header)
            for step in range(25 * 256 + 1):
                time = step * 4e-9 / 256
                # The frontend is 18 mV from DC at the aperture; the sampler
                # adds 3 mV. Individual stages and the raw ADC keep budgets.
                input_value, held = 1.65 - .018, 1.65 - .021
                if fault == 'frontend':
                    input_value -= .03
                    held -= .03
                if fault == 'sampler':
                    held -= .03
                if fault == 'hold' and time > 30e-9:
                    held += .03
                writer.writerow([time, 1.77, 1.53, 2.5, input_value, 3.299, 1.2, held, 0])
        names = ['clock', 'data', 'valid', 'slice', 'fault', 'convert', 'normalized', 'normalized_valid',
                 'normalized_slice', 'clipped']
        with (work / 'controls.csv').open('w') as stream:
            writer = csv.DictWriter(stream, names)
            writer.writeheader()
            for clock in range(-1, 25):
                row = dict.fromkeys(names, 0)
                row.update(clock=clock, data=131 if fault == 'code' else 128, valid=int(clock == 13),
                           normalized=128, normalized_valid=int(clock == 21), convert=int(4 <= clock <= 11))
                writer.writerow(row)
        return work

    def test_distinct_stage_budgets_retain_end_to_end_code_check(self):
        with tempfile.TemporaryDirectory() as directory:
            work = self.experiment(directory)
            result = check(work, 'cpp', 1, 1, transfer=lambda p, m, v: (2.5, 1.65))
            self.assertTrue(all(result['checks'].values()))
            self.assertGreater(result['maximum_hold_error_V'], .02)
            self.assertLess(result['maximum_sampler_acquisition_error_V'], .004)

    def test_each_independent_stage_and_end_to_end_fault_is_rejected(self):
        for fault, expectation in (('frontend', 'frontend_at_aperture'), ('sampler', 'sampler_acquisition'),
                                   ('hold', 'hold'), ('code', 'conversion_accuracy')):
            with self.subTest(fault=fault), tempfile.TemporaryDirectory() as directory:
                work = self.experiment(directory, fault)
                with self.assertRaisesRegex(ValueError, 'contract failed'):
                    check(work, 'cpp', 1, 1, transfer=lambda p, m, v: (2.5, 1.65))
                result = json.loads((work / 'failed_contract.json').read_text())
                self.assertFalse(result['checks'][expectation])


if __name__ == '__main__':
    unittest.main()
