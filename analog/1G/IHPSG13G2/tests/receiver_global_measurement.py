"""RC screening must preserve nominal timing and reject missing/invalid values."""
import copy
from pathlib import Path
import sys
import unittest

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))
from receiver_global_measurement import check_restoration


class Measurement(unittest.TestCase):
    def setUp(self):
        self.proof = dict(rounds=[dict(setup_ns=.2, hold_ns=.13)])
        self.estimates = {name: dict(setup_ns=.2, hold_ns=.13, electrical_pass=True)
                          for name in ('before', 'margin', 'restored')}

    def test_passing_screen_and_failed_margin_are_distinct(self):
        self.assertTrue(check_restoration(self.proof, self.estimates))
        for key, value in (('setup_ns', -.8), ('hold_ns', .09), ('electrical_pass', False)):
            estimates = copy.deepcopy(self.estimates)
            estimates['margin'][key] = value
            self.assertFalse(check_restoration(self.proof, estimates))

    def test_changed_nominal_timing_is_not_a_completed_measurement(self):
        for name in ('before', 'restored'):
            for key in ('setup_ns', 'hold_ns'):
                estimates = copy.deepcopy(self.estimates)
                estimates[name][key] += .002
                with self.subTest(name=name, key=key), self.assertRaises(ValueError):
                    check_restoration(self.proof, estimates)

    def test_missing_or_nonfinite_values_cannot_pass(self):
        estimates = copy.deepcopy(self.estimates)
        del estimates['margin']
        with self.assertRaises(ValueError):
            check_restoration(self.proof, estimates)
        for value in (float('nan'), float('inf'), True, None):
            estimates = copy.deepcopy(self.estimates)
            estimates['margin']['setup_ns'] = value
            with self.subTest(value=value), self.assertRaises(ValueError):
                check_restoration(self.proof, estimates)


if __name__ == '__main__':
    unittest.main()
