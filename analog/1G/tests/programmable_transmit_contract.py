"""Check both measured trim domains, including a disconnected final branch."""
import json
from pathlib import Path
import tempfile
import unittest

from core_transmit_driver import HEADER, check_trim_delivery, qualified_controls


class TrimDelivery(unittest.TestCase):
    def rows(self):
        # Code 26 enables branches 1, 3 and 4. All other branches stay off.
        return [[0] * len(HEADER) + [1.2, 3.3, 0, 0, 1.2, 3.3, 0, 0, 0, 0, 1.2, 3.3]]

    def test_active_low_bits_reach_both_voltage_domains(self):
        self.assertTrue(check_trim_delivery(self.rows(), 26)['every_trim_bit_delivered'])

    def test_missing_changed_nonfinite_or_untranslated_final_bit_fails(self):
        for fault in ('missing', 'polarity', 'untranslated', 'unresolved', 'nonfinite'):
            rows = self.rows()
            if fault == 'missing':
                rows[0].pop()
            elif fault == 'polarity':
                rows[0][-2:] = [0, 0]
            elif fault == 'untranslated':
                rows[0][-1] = 1.2
            elif fault == 'unresolved':
                rows[0][-2] = .6
            else:
                rows[0][-1] = float('nan')
            with self.subTest(fault=fault), self.assertRaises(ValueError):
                check_trim_delivery(rows, 26)

    def test_bad_code_or_missing_observations_fails(self):
        for code in (-1, 64, None):
            with self.subTest(code=code), self.assertRaises(ValueError):
                check_trim_delivery(self.rows(), code)
        with self.assertRaises(ValueError):
            check_trim_delivery([], 26)

    def test_unqualified_or_unidentified_controller_fixture_fails(self):
        with tempfile.TemporaryDirectory() as directory:
            path = Path(directory) / 'checks.json'
            for data in ({'status': 'fail'}, {'status': 'pass', 'flow': 'unqualified'},
                         {'status': 'pass', 'flow': 'control_cpp', 'module': 'WrongController'}):
                path.write_text(json.dumps(data))
                with self.assertRaises(ValueError):
                    qualified_controls(path)


if __name__ == '__main__':
    unittest.main()
