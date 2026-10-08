#!/usr/bin/env python3
"""Exercise bootstrap safety without running compilers or touching real checkouts."""
import os
from pathlib import Path
import shutil
import subprocess
import tempfile
import unittest

SCRIPT = Path(__file__).resolve().parents[1] / '.build_chipyard.sh'


class ReuseChipyardTest(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory()
        self.addCleanup(self.temp.cleanup)
        self.base = Path(self.temp.name)
        self.product = self.base / 'cpphdl/firtool'
        self.product.mkdir(parents=True)
        shutil.copy2(SCRIPT, self.product / SCRIPT.name)
        self.source = self.base / 'existing chipyard'
        (self.source / 'sims/verilator').mkdir(parents=True)
        (self.source / 'build.sbt').write_text('local changes\n')
        prefix = self.source / '.conda-env/riscv-tools'
        for name in ('lib/libfesvr.a', 'bin/riscv64-unknown-elf-gcc'):
            path = prefix / name
            path.parent.mkdir(parents=True, exist_ok=True)
            path.touch()
        self.marker = self.source / '.conda-env/keep-me'
        self.marker.write_text('prepared environment\n')
        self.log = self.base / 'calls'
        for name in ('.build_rocket64_cpphdl.sh', '.run_rocket64_cpphdl.sh'):
            path = self.product / name
            path.write_text('#!/bin/sh\nprintf "%s\\n" "$0" >> "$TEST_LOG"\n')
            path.chmod(0o755)
        self.env = {**os.environ, 'CHIPYARD_SOURCE_DIR': str(self.source),
                    'CPPHDL_TOOL': '/bin/true', 'TEST_LOG': str(self.log),
                    'CHIPYARD_RUN_SMOKE_TESTS': '1'}

    def run_bootstrap(self):
        return subprocess.run(['bash', str(self.product / SCRIPT.name)],
                              env=self.env, text=True, capture_output=True)

    def test_reuse_and_repeat_preserve_checkout(self):
        for _ in range(2):
            result = self.run_bootstrap()
            self.assertEqual(result.returncode, 0, result.stderr)
        self.assertTrue((self.product / 'chipyard').is_symlink())
        self.assertEqual((self.product / 'chipyard').resolve(), self.source)
        self.assertEqual(self.marker.read_text(), 'prepared environment\n')
        self.assertEqual((self.source / 'build.sbt').read_text(), 'local changes\n')
        self.assertEqual(len(self.log.read_text().splitlines()), 4)

    def test_sourcing_keeps_native_helpers_without_building(self):
        result = subprocess.run(
            ['bash', '-c', 'source "$1"; declare -F build_simulator', '_',
             str(self.product / SCRIPT.name)], env=self.env, capture_output=True, text=True)
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertIn('build_simulator', result.stdout)
        self.assertFalse(self.log.exists())

    def test_build_without_simulation(self):
        self.env['CHIPYARD_RUN_SMOKE_TESTS'] = '0'
        self.assertEqual(self.run_bootstrap().returncode, 0)
        self.assertEqual(len(self.log.read_text().splitlines()), 1)

    def test_missing_source_does_not_clone(self):
        self.env['CHIPYARD_SOURCE_DIR'] = str(self.base / 'missing')
        self.assertNotEqual(self.run_bootstrap().returncode, 0)
        self.assertFalse((self.product / 'chipyard').exists())
        self.assertFalse(self.log.exists())

    def test_broken_link_is_not_replaced(self):
        link = self.product / 'chipyard'
        link.symlink_to(self.base / 'missing')
        self.assertNotEqual(self.run_bootstrap().returncode, 0)
        self.assertEqual(link.readlink(), self.base / 'missing')
        self.assertFalse(self.log.exists())

    def test_different_existing_checkout_is_not_retargeted(self):
        other = self.base / 'other'
        (other / 'sims/verilator').mkdir(parents=True)
        (other / 'build.sbt').touch()
        link = self.product / 'chipyard'
        link.symlink_to(other)
        self.assertNotEqual(self.run_bootstrap().returncode, 0)
        self.assertEqual(link.resolve(), other)
        self.assertFalse(self.log.exists())


if __name__ == '__main__':
    unittest.main()
