"""A final trace cannot silently disagree with earlier electrical decisions."""
import csv
import importlib.util
import json
from pathlib import Path
import tempfile
import unittest

spec = importlib.util.spec_from_file_location('replay_voltage_contract',
    Path(__file__).resolve().parents[1] / 'replay_consistency.py')
contract = importlib.util.module_from_spec(spec)
spec.loader.exec_module(contract)


class ReplayConsistency(unittest.TestCase):
    def fixture(self, root):
        (root / 'columns.csv').write_text('time_s,CORE_DECISION0\n')
        (root / 'spice.dat').write_text('time observation0\n' +
            ''.join(f'{step * 1e-10:.17g} {1.2 if step >= 25 else 0}\n' for step in range(121)))
        (root / 'live.json').write_text(json.dumps(dict(clocks=3, period_s=4e-9, slices=1,
                                                     feedback_low_V=.36, feedback_high_V=.84)))
        with (root / 'controls.csv').open('w') as stream:
            writer = csv.writer(stream)
            writer.writerow(['clock', 'convert'])
            writer.writerows([(-1, 0), (0, 1), (1, 0), (2, 0)])
        decisions = [dict(clock=i, time_s=(i + .5) * 4e-9, sampled=i == 1,
                          voltage_V=[1.2 if i == 1 else None], feedback=int(i == 1)) for i in range(3)]
        (root / 'decisions.json').write_text(json.dumps(decisions))
        return decisions

    def test_all_required_feedback_reads_match_the_complete_trace(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            self.fixture(root)
            result = contract.check_replayed_decisions(root, 1)
            self.assertEqual(result['decision_reads'], 1)
            self.assertEqual(result['maximum_decision_replay_difference_V'], 0)

    def test_voltage_drift_wrong_bits_and_omitted_reads_fail(self):
        for fault in ('drift', 'polarity', 'missing', 'coverage', 'unsampled'):
            with self.subTest(fault=fault), tempfile.TemporaryDirectory() as directory:
                root = Path(directory)
                decisions = self.fixture(root)
                if fault == 'drift':
                    decisions[1]['voltage_V'] = [1.15]
                elif fault == 'polarity':
                    decisions[1]['feedback'] = 0
                elif fault == 'missing':
                    decisions[1]['sampled'] = False
                elif fault == 'coverage':
                    decisions.pop()
                else:
                    decisions[0]['voltage_V'] = [0]
                (root / 'decisions.json').write_text(json.dumps(decisions))
                with self.assertRaises(ValueError):
                    contract.check_replayed_decisions(root, 1)


if __name__ == '__main__':
    unittest.main()
