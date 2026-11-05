"""The EDA memory guard must retain exit status and terminate owned jobs."""
import json
from pathlib import Path
import subprocess
import sys
import tempfile
import unittest

RUNNER = Path(__file__).resolve().parents[1] / 'shared_memory_run.py'

class MemoryGuard(unittest.TestCase):
    def run_child(self, code, memory=128, seconds=5):
        with tempfile.TemporaryDirectory() as work:
            report = Path(work) / 'report.json'
            result = subprocess.run([sys.executable, str(RUNNER), '--memory-mib', str(memory),
                '--seconds', str(seconds), '--report', str(report), '--', sys.executable, '-c', code],
                capture_output=True, text=True, timeout=10)
            return result.returncode, json.loads(report.read_text())

    def test_child_exit_is_retained(self):
        code, report = self.run_child('raise SystemExit(7)')
        self.assertEqual(code, 7)
        self.assertIsNone(report['stopped_for'])

    def test_memory_limit_terminates_child(self):
        code, report = self.run_child('import time; storage = bytearray(80 * 1024 ** 2); time.sleep(3)', memory=32)
        self.assertEqual(code, 137)
        self.assertEqual(report['stopped_for'], 'resident_memory_limit')

    def test_time_limit_terminates_child(self):
        code, report = self.run_child('import time; time.sleep(3)', seconds=1)
        self.assertEqual(code, 137)
        self.assertEqual(report['stopped_for'], 'time_limit')

if __name__ == '__main__':
    unittest.main()
