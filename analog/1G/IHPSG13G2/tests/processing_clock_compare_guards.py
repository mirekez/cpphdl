"""Failed or stale oscillator evidence must replace an earlier pass receipt."""
import json
from pathlib import Path
import tempfile
import unittest

from processing_clock_compare import CASES, compare


class ComparisonEvidence(unittest.TestCase):
    def test_missing_report_cannot_retain_previous_pass(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            output = root / 'checks.json'
            output.write_text('{"status": "pass"}\n')
            with self.assertRaises(FileNotFoundError):
                compare(root / 'missing-cpp.json', root / 'missing-va.json', output)
            self.assertEqual('fail', json.loads(output.read_text())['status'])

    def test_stale_source_cannot_retain_previous_pass(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            source = root / 'circuit.hpp'
            source.write_text('changed circuit\n')
            paths = [root / (flow + '.json') for flow in ('cpp', 'veriloga')]
            for path, flow in zip(paths, ('cpp', 'veriloga')):
                path.write_text(json.dumps(dict(status='pass', flow=flow,
                    cases={name: {} for name in CASES},
                    source_sha256={str(source): '0' * 64})))
            output = root / 'checks.json'
            output.write_text('{"status": "pass"}\n')
            with self.assertRaisesRegex(ValueError, 'stale oscillator'):
                compare(*paths, output)
            report = json.loads(output.read_text())
            self.assertEqual('fail', report['status'])
            self.assertIn('stale', report['error'])


if __name__ == '__main__':
    unittest.main()
