"""Test build setup without requiring any particular host sanitizer runtime."""
from pathlib import Path
import subprocess
import tempfile
import unittest
from unittest.mock import patch

from TestToolchain import TestToolchain


class ToolchainChecks(unittest.TestCase):
    def setUp(self):
        self.directory = tempfile.TemporaryDirectory()
        self.addCleanup(self.directory.cleanup)
        self.environment = patch.dict('os.environ', {}, clear=True)
        self.environment.start()
        self.addCleanup(self.environment.stop)
        with patch('subprocess.check_output', return_value='libstdc++.so\n'):
            self.toolchain = TestToolchain('/tools/bin/clang++', '/tools/bin/verilator', self.directory.name)

    def test_verilator_uses_selected_compiler(self):
        command = self.toolchain.command(['/tools/bin/verilator', '--binary', 'test.sv'])
        self.assertEqual(command[command.index('-MAKEFLAGS') + 1],
                         'CXX=/tools/bin/clang++ LINK=/tools/bin/clang++ AR=ar')
        self.assertIn('-L/tools/lib', command)
        self.assertIn('-std=c++20', command)

    def test_ordinary_commands_are_unchanged(self):
        command = ['/tmp/test', '--run']
        with patch.object(self.toolchain, 'probe') as probe:
            self.assertEqual(self.toolchain.command(command), command)
            probe.assert_not_called()

    def test_timing_does_not_downgrade_requested_standard(self):
        command = self.toolchain.command(['/tools/bin/verilator', '--binary',
                                           '-CFLAGS', '-std=c++23', 'test.sv'])
        self.assertNotIn('-std=c++20', command)
        self.assertIn('-std=c++23', command)

    def test_asan_probe_cached(self):
        with patch.object(self.toolchain, 'probe', return_value=subprocess.CompletedProcess([], 0, '', '')) as probe:
            for _ in range(2):
                self.assertIn('-fsanitize=address,undefined', self.toolchain.sanitizers())
            probe.assert_called_once()

    def test_only_known_startup_conflict_falls_back(self):
        conflict = subprocess.CompletedProcess([], 1, '',
            'RTLD_DEEPBIND is incompatible with sanitizer runtime')
        success = subprocess.CompletedProcess([], 0, '', '')
        with patch.object(self.toolchain, 'probe', side_effect=[conflict, success]) as probe:
            self.assertEqual(self.toolchain.sanitizers(),
                             ['-fsanitize=undefined', '-fno-sanitize-recover=all'])
            self.assertEqual(probe.call_count, 2)

    def test_unexpected_probe_error_is_fatal(self):
        with patch.object(self.toolchain, 'probe', return_value=
                          subprocess.CompletedProcess([], 1, '', 'heap-buffer-overflow')) as probe:
            with self.assertRaisesRegex(RuntimeError, 'heap-buffer-overflow'):
                self.toolchain.sanitizers()
            probe.assert_called_once()

    def test_explicit_asan_never_falls_back(self):
        with patch.dict('os.environ', {'CPPHDL_TEST_SANITIZERS': 'address,undefined'}), \
             patch.object(self.toolchain, 'probe', return_value=subprocess.CompletedProcess([], 1, '',
                 'RTLD_DEEPBIND is incompatible with sanitizer runtime')) as probe:
            with self.assertRaises(RuntimeError):
                self.toolchain.sanitizers()
            probe.assert_called_once()


if __name__ == '__main__':
    unittest.main()
