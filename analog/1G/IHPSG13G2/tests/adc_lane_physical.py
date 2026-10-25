#!/usr/bin/env python3
"""Reject stale or incomplete lane evidence before physical implementation."""
import json
from pathlib import Path
import sys
import tempfile
import unittest

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))
from adc_lane_physical import checked_proof, digest, PORTS, TOP


class LaneProof(unittest.TestCase):
    def fixture(self, root):
        source = root / 'lane.hpp'
        source.write_text('qualified source')
        for name in ('mapped.v', 'mapped_interface.sv', 'cells.v'):
            (root / name).write_text('checked ' + name)
        (root / 'mapped.json').write_text(json.dumps({'modules': {TOP: {'ports': {
            name: {'bits': list(range(width))} for name, width in PORTS.items()}}}}))
        for name in ('native.csv', 'mapped.csv'):
            (root / name).write_text('checked clock\n' * 15468)
        trace = digest(root / 'native.csv')
        return dict(status='pass', work=str(root), source_sha256={str(source): digest(source)},
            artifact_sha256={str(root / name): digest(root / name) for name in
                ('mapped.v', 'mapped.json', 'mapped_interface.sv', 'cells.v', 'native.csv', 'mapped.csv')},
            native_trace_sha256=trace, stages={stage: dict(status='pass', checked_clocks=15468,
                trace_sha256=trace) for stage in ('rtl', 'mapped')})

    def test_qualified_inputs_and_stale_source_or_artifact(self):
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            proof = self.fixture(root)
            receipt = root / 'checks.json'
            receipt.write_text(json.dumps(proof))
            self.assertEqual(checked_proof(receipt)[0], root)
            (root / 'mapped.v').write_text('changed gates')
            with self.assertRaisesRegex(ValueError, 'changed initialized-lane artifact'):
                checked_proof(receipt)
            proof = self.fixture(root)
            receipt.write_text(json.dumps(proof))
            (root / 'lane.hpp').write_text('new source')
            with self.assertRaisesRegex(ValueError, 'stale initialized-lane'):
                checked_proof(receipt)

    def test_missing_mapping_stage_and_changed_port_contract(self):
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            proof = self.fixture(root)
            receipt = root / 'checks.json'
            proof['stages'].pop('mapped')
            receipt.write_text(json.dumps(proof))
            with self.assertRaisesRegex(ValueError, 'native/RTL/mapped'):
                checked_proof(receipt)
            proof = self.fixture(root)
            module = json.loads((root / 'mapped.json').read_text())
            module['modules'][TOP]['ports']['trial_out']['bits'].pop()
            (root / 'mapped.json').write_text(json.dumps(module))
            proof['artifact_sha256'][str(root / 'mapped.json')] = digest(root / 'mapped.json')
            receipt.write_text(json.dumps(proof))
            with self.assertRaisesRegex(ValueError, 'physical port contract'):
                checked_proof(receipt)

    def test_truncated_equal_traces_cannot_claim_the_full_run(self):
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            proof = self.fixture(root)
            for name in ('native.csv', 'mapped.csv'):
                (root / name).write_text('checked clock\n' * 20)
                proof['artifact_sha256'][str(root / name)] = digest(root / name)
            trace = digest(root / 'native.csv')
            proof['native_trace_sha256'] = trace
            for stage in ('rtl', 'mapped'):
                proof['stages'][stage]['trace_sha256'] = trace
            receipt = root / 'checks.json'
            receipt.write_text(json.dumps(proof))
            with self.assertRaisesRegex(ValueError, 'incomplete initialized-lane trajectory'):
                checked_proof(receipt)


if __name__ == '__main__':
    unittest.main()
