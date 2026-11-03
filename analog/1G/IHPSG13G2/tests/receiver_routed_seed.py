"""Early routed wires may guide sizing but cannot inherit a routing pass."""
import copy
import json
from pathlib import Path
import sys
import tempfile
import unittest

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))
from receiver_routed_seed import admit, digest


class RoutedSeed(unittest.TestCase):
    def fixture(self, work):
        files = [work / n for n in ('candidate.odb', 'candidate.sdc', 'after.v', 'cells.v')]
        for p in files:
            p.write_text(p.name)
        hashes = {str(p): digest(p) for p in files}
        proof = dict(status='diagnostic_complete', cell_models=str(files[-1]),
            source_sha256=hashes, artifact_sha256=hashes,
            rounds=[dict(name='after', connectivity=dict(status='pass', top='PCSReceiver'),
                         setup_ns=.2, hold_ns=.15, electrical_pass=True)])
        (work / 'checks.json').write_text(json.dumps(proof))
        return dict(status='early_route_diagnostic_complete', stage='complete', detail_end_iteration=1,
                    connectivity=dict(status='pass', top='PCSReceiver'), timing_qualified=False,
                    physical_qualified=False, full_chip_qualified=False,
                    source_sha256=dict(hashes, **{str(work / 'checks.json'): digest(work / 'checks.json')}))

    def test_early_requires_origin_and_preserves_scope(self):
        with tempfile.TemporaryDirectory() as temporary:
            work = Path(temporary)
            proof = self.fixture(work)
            with self.assertRaisesRegex(ValueError, 'explicit probe origin'):
                admit(proof)
            hashes, scope = admit(proof, work)
            self.assertEqual(scope, 'early_route_diagnostic')
            self.assertEqual(hashes[str(work / 'cells.v')], digest(work / 'cells.v'))

    def test_reject_changed_candidate_or_qualification_claim(self):
        with tempfile.TemporaryDirectory() as temporary:
            work = Path(temporary)
            proof = self.fixture(work)
            for key in ('timing_qualified', 'physical_qualified', 'full_chip_qualified'):
                bad = copy.deepcopy(proof)
                bad[key] = True
                with self.assertRaisesRegex(ValueError, 'claim qualification'):
                    admit(bad, work)
            (work / 'candidate.odb').write_text('changed')
            with self.assertRaisesRegex(ValueError, 'another candidate'):
                admit(proof, work)

    def test_incomplete_route_rejected(self):
        proof = dict(status='routed_timing_fail', stage='complete',
                     routed_connectivity=dict(status='pass', top='PCSReceiver'))
        self.assertEqual(admit(proof)[1], 'full_route_diagnostic')
        proof['stage'] = 'extract'
        with self.assertRaises(ValueError):
            admit(proof)


if __name__ == '__main__':
    unittest.main()
