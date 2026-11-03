"""Failed timing may supply a candidate; incomplete logical evidence may not."""
from copy import deepcopy
from pathlib import Path
import sys
import unittest

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))
from receiver_pin_stage import require_origin


class PinStage(unittest.TestCase):
    def setUp(self):
        self.proof = dict(status='fail', stages=[dict(name='global', returncode=0)],
                          global_electrical_pass=True)
        for key in ('seed_connectivity', 'filler_connectivity', 'buffer_connectivity', 'placement_connectivity'):
            self.proof[key] = dict(status='pass')

    def test_completed_timing_failure_may_supply_diagnostic_geometry(self):
        require_origin(self.proof)
        self.proof['status'] = 'global_repair_candidate'
        require_origin(self.proof)

    def test_running_or_failed_native_stage_is_rejected(self):
        for change in ({'status': 'running'}, {'stages': []},
                       {'stages': [dict(name='global', returncode=1)]}):
            proof = dict(self.proof, **change)
            with self.subTest(change=change), self.assertRaisesRegex(ValueError, 'completed global'):
                require_origin(proof)

    def test_every_preceding_logical_proof_is_required(self):
        for key in ('seed_connectivity', 'filler_connectivity', 'buffer_connectivity', 'placement_connectivity'):
            proof = deepcopy(self.proof)
            proof[key]['status'] = 'fail'
            with self.subTest(key=key), self.assertRaisesRegex(ValueError, 'connectivity proof'):
                require_origin(proof)

    def test_pin_only_repair_cannot_hide_electrical_violations(self):
        for value in (None, False, 'pass'):
            self.proof['global_electrical_pass'] = value
            with self.subTest(value=value), self.assertRaisesRegex(ValueError, 'electrical violations'):
                require_origin(self.proof)


if __name__ == '__main__':
    unittest.main()
