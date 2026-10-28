#!/usr/bin/env python3
"""Check that physical controller inputs retain their actual functional proof."""
import gzip
import json
from pathlib import Path
import sys
import tempfile
import unittest

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))
from prepare_processing_clock_physical import prepare, digest


class ControllerSnapshot(unittest.TestCase):
    def setUp(self):
        self.temporary = tempfile.TemporaryDirectory()
        self.addCleanup(self.temporary.cleanup)
        self.root = Path(self.temporary.name)
        design = self.root / 'controller.hpp'
        design.write_text('source used by the independent qualification fixture\n')
        rtl = self.root / 'controller.sv'
        rtl.write_text('generated rtl\n')
        trace = self.root / 'trace.csv'
        trace.write_text('fixture waveform\n')
        archived = self.root / 'trace.csv.gz'
        with gzip.open(archived, 'wb') as output:
            output.write(trace.read_bytes())
        self.record = dict(path=str(archived), archive_sha256=digest(archived), raw_sha256=digest(trace))
        self.native = self.root / 'native.json'
        self.cell = self.root / 'cells.json'
        self.mapping = self.root / 'mapping'
        self.mapping.mkdir()
        for name in ('mapped.v', 'cells.v'):
            (self.mapping / name).write_text(name + '\n')
        (self.mapping / 'mapped.json').write_text(json.dumps({'modules': {'ProcessingClockControl':
            {'ports': {'pcs_clk': {}}, 'cells': {'ff': {}}}}}))
        native = dict(status='pass', events=698640, qualified_curves=5, rejected_curves=3, load_step_cases=6, drift_recoveries=2,
                      source_sha256={str(design): digest(design)}, rtl_sha256={str(rtl): digest(rtl)},
                      traces=dict(native=self.record, rtl=self.record))
        self.native.write_text(json.dumps(native))
        self.cells = dict(status='pass', acquisition_period_ns=1.25, reference_period_ns=8,
                          setup_slack_ns=.17, work=str(self.mapping),
                          source_sha256={str(design): digest(design), str(self.native): digest(self.native)},
                          artifact_sha256={str(p): digest(p) for p in self.mapping.iterdir()},
                          traces=dict(mapped=self.record, resized=self.record))
        self.save()

    def save(self):
        self.cell.write_text(json.dumps(self.cells))

    def test_exact_snapshot_retains_netlist_and_native_trace(self):
        output = self.root / 'physical'
        result = prepare(self.native, self.cell, output)
        self.assertEqual(result['status'], 'pass')
        self.assertEqual(digest(output / 'cpp_trace.csv'), self.record['raw_sha256'])
        self.assertEqual(digest(output / 'mapped.v'), digest(self.mapping / 'mapped.v'))
        self.assertIn(str(self.native), result['source_sha256'])
        with self.assertRaisesRegex(ValueError, 'fresh'):
            prepare(self.native, self.cell, output)

    def test_failed_nonfinite_or_old_clock_budget_is_rejected(self):
        for key, value in [('status', 'fail'), ('setup_slack_ns', -.01),
                           ('setup_slack_ns', float('nan')), ('setup_slack_ns', float('inf')),
                           ('acquisition_period_ns', 2.5)]:
            original = self.cells[key]
            self.cells[key] = value
            self.save()
            with self.subTest(key=key, value=value), self.assertRaisesRegex(ValueError, 'complete passing'):
                prepare(self.native, self.cell, self.root / 'physical')
            self.cells[key] = original

    def test_changed_mapping_or_waveform_is_rejected(self):
        mapped = self.mapping / 'mapped.json'
        original = mapped.read_bytes()
        mapped.write_text('{}')
        with self.assertRaisesRegex(ValueError, 'changed controller evidence'):
            prepare(self.native, self.cell, self.root / 'physical')
        mapped.write_bytes(original)
        Path(self.record['path']).write_bytes(b'corrupt waveform')
        with self.assertRaisesRegex(ValueError, 'compressed controller trajectory'):
            prepare(self.native, self.cell, self.root / 'physical')

    def test_matching_but_untracked_mapping_is_rejected(self):
        del self.cells['artifact_sha256'][str(self.mapping / 'mapped.json')]
        self.save()
        with self.assertRaisesRegex(ValueError, 'untracked physical input'):
            prepare(self.native, self.cell, self.root / 'physical')

    def test_old_proof_without_admission_load_cases_is_rejected(self):
        proof = json.loads(self.native.read_text())
        del proof['load_step_cases']
        self.native.write_text(json.dumps(proof))
        with self.assertRaisesRegex(ValueError, 'complete passing'):
            prepare(self.native, self.cell, self.root / 'physical')

    def test_old_proof_without_drift_recovery_is_rejected(self):
        proof = json.loads(self.native.read_text())
        del proof['drift_recoveries']
        self.native.write_text(json.dumps(proof))
        with self.assertRaisesRegex(ValueError, 'complete passing'):
            prepare(self.native, self.cell, self.root / 'physical')


if __name__ == '__main__':
    unittest.main()
