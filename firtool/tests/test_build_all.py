#!/usr/bin/env python3
"""Check orchestration order and failure propagation without building Chipyard."""
import os
from pathlib import Path
import shutil
import subprocess
import tempfile
import unittest
from version_fixture import ROOT


class BuildAll(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory(prefix='cpphdl build all ')
        self.addCleanup(self.temp.cleanup)
        self.product = Path(self.temp.name)
        shutil.copy2(ROOT / '.build_all.sh', self.product / '.build_all.sh')
        self.log = self.product / 'calls'
        (self.product / '.build_chipyard.sh').write_text(
            'step() { printf "%s\\n" "$*"; }\n'
            'require_clean_enough_checkout() { printf "validate\\n" >> "$TEST_LOG"; '
            '[[ "${FAIL_AT:-}" != validate ]]; }\n')
        self.builds = ['.chipyard_cpphdl_patch.sh', '.build_rocket64_verilator.sh',
                       '.build_rocket64_cpphdl.sh', '.build_rocket64_cpphdl-optimize-combs.sh',
                       '.build_rocket64_cpphdl-native-graph.sh']
        self.runs = ['.run_rocket64_verilator.sh', '.run_rocket64_cpphdl.sh',
                     '.run_rocket64_cpphdl-optimize-combs.sh', '.run_rocket64_cpphdl-native-graph.sh']
        for name in self.builds + self.runs:
            script = self.product / name
            script.write_text('#!/bin/bash\nname=${0##*/}\n'
                              'printf "%s\\n" "$name" >> "$TEST_LOG"\n'
                              '[[ "${FAIL_AT:-}" != "$name" ]]\n')
            script.chmod(0o755)

    def run_build(self, *arguments, fail=''):
        return subprocess.run(['bash', str(self.product / '.build_all.sh'), *arguments],
                              text=True, capture_output=True, timeout=10,
                              env={**os.environ, 'TEST_LOG': str(self.log), 'FAIL_AT': fail})

    def test_build_order(self):
        result = self.run_build()
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertEqual(self.log.read_text().splitlines(), ['validate', *self.builds])

    def test_smoke_tests_run_after_all_builds(self):
        result = self.run_build('--run-tests')
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertEqual(self.log.read_text().splitlines(), ['validate', *self.builds, *self.runs])

    def test_failure_stops_following_steps(self):
        for failing in ['validate', *self.builds]:
            with self.subTest(failing=failing):
                self.log.unlink(missing_ok=True)
                result = self.run_build('--run-tests', fail=failing)
                self.assertNotEqual(result.returncode, 0)
                expected = ['validate', *self.builds]
                self.assertEqual(self.log.read_text().splitlines(), expected[:expected.index(failing)+1])

    def test_help_does_not_access_checkout(self):
        result = self.run_build('--help')
        self.assertEqual(result.returncode, 0)
        self.assertIn('Usage:', result.stdout)
        self.assertFalse(self.log.exists())

    def test_unknown_option_does_not_build(self):
        self.assertNotEqual(self.run_build('--unknown').returncode, 0)
        self.assertFalse(self.log.exists())


if __name__ == '__main__':
    unittest.main()
