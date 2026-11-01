#!/usr/bin/env python3
"""Reject incomplete or stale layout evidence before launching a long PLL run."""
import copy
import hashlib
import json
from pathlib import Path
import runpy
import tempfile
import unittest

fixture = runpy.run_path(str(Path(__file__).with_name('reference_clock_realized_extracted.py')))
layout_evidence = fixture['layout_evidence']


class Evidence(unittest.TestCase):
    def test_required_geometry_and_provenance(self):
        with tempfile.TemporaryDirectory() as directory:
            work = Path(directory)
            source, gds, proof = work / 'source.py', work / 'clock.gds', work / 'checks.json'
            source.write_text('test source identity\n')
            gds.write_bytes(b'test artifact identity; no geometry qualification claimed')
            digest = lambda path: hashlib.sha256(path.read_bytes()).hexdigest()
            valid = dict(status='pass', filled=True, full_drc_pass=True, drc_violations={},
                disconnected_reference_rejected=True, lvs=dict(instantiated_devices=259),
                geometry=dict(substrate_net='VSS'), work=str(work),
                source_sha256={str(source): digest(source)}, artifact_sha256={str(gds): digest(gds)})
            proof.write_text(json.dumps(valid))
            self.assertEqual(layout_evidence(proof)[1], gds)
            failures = [dict(status='running'), dict(filled=False), dict(full_drc_pass=False),
                dict(drc_violations={'M1.j': 1}), dict(disconnected_reference_rejected=False),
                dict(lvs=dict(instantiated_devices=526)), dict(geometry=dict(substrate_net='PSUB')),
                dict(source_sha256={}), dict(artifact_sha256={}),
                dict(artifact_sha256={str(source): digest(source)})]
            for change in failures:
                with self.subTest(change=change):
                    bad = copy.deepcopy(valid)
                    bad.update(change)
                    proof.write_text(json.dumps(bad))
                    with self.assertRaises(ValueError):
                        layout_evidence(proof)
            proof.write_text(json.dumps(valid))
            for path in (source, gds):
                with self.subTest(stale=path.name):
                    original = path.read_bytes()
                    path.write_bytes(original + b'changed')
                    with self.assertRaises(ValueError):
                        layout_evidence(proof)
                    path.write_bytes(original)


if __name__ == '__main__':
    unittest.main()
