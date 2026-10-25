"""Test build setup without requiring any particular host sanitizer runtime."""
import os
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

    def test_atomic_library_directory_comes_from_selected_compiler(self):
        root = Path(self.directory.name).resolve()
        libraries = {}
        for name, directory in (('libstdc++.so', 'cxx runtime'), ('libatomic.so', 'atomic runtime')):
            path = root / directory / name
            path.parent.mkdir()
            path.touch()
            libraries[name] = str(path)
        with patch('subprocess.check_output', side_effect=lambda command, **kwargs:
                   libraries[command[1].split('=', 1)[1]]):
            toolchain = TestToolchain('/tools/bin/clang++', '/tools/bin/verilator', root)
        self.assertIn(str(root / 'atomic runtime'), toolchain.library_dirs)
        self.assertIn(str(root / 'atomic runtime'), toolchain.env['LD_LIBRARY_PATH'].split(':'))
        command = toolchain.command(['/tools/bin/verilator', '--build', 'test.sv'])
        self.assertIn("-L'" + str(root / 'atomic runtime') + "'", command[command.index('-LDFLAGS') + 1])

    def test_timing_does_not_downgrade_requested_standard(self):
        command = self.toolchain.command(['/tools/bin/verilator', '--binary',
                                           '-CFLAGS', '-std=c++23', 'test.sv'])
        self.assertNotIn('-std=c++20', command)
        self.assertIn('-std=c++23', command)

    def test_selected_runtime_precedes_inherited_runtime(self):
        library = Path(self.directory.name).resolve() / 'compiler-runtime' / 'libstdc++.so'
        library.parent.mkdir()
        library.touch()
        with patch.dict('os.environ', {'LD_LIBRARY_PATH': '/old/runtime:/application/lib'}), \
             patch('subprocess.check_output', return_value=str(library)):
            toolchain = TestToolchain('/tools/bin/clang++', None, self.directory.name)
        paths = toolchain.env['LD_LIBRARY_PATH'].split(':')
        self.assertEqual(paths[0], str(library.parent))
        self.assertEqual(paths[-2:], ['/old/runtime', '/application/lib'])
        self.assertNotIn('LD_LIBRARY_PATH', os.environ)

    def test_symlinked_compiler_library_paths_are_resolved(self):
        root = Path(self.directory.name).resolve()
        runtime = root / 'real runtime'
        runtime.mkdir()
        for name in ('libstdc++.so', 'libatomic.so'):
            (runtime / name).touch()
        alias = root / 'runtime alias'
        alias.symlink_to(runtime, target_is_directory=True)
        with patch('subprocess.check_output', side_effect=lambda command, **kwargs:
                   str(alias / command[1].split('=', 1)[1])):
            toolchain = TestToolchain('/tools/bin/clang++', '/tools/bin/verilator', root)
        self.assertEqual(toolchain.library_dirs.count(str(runtime)), 1)
        self.assertNotIn(str(alias), toolchain.library_dirs)
        self.assertEqual(toolchain.env['LD_LIBRARY_PATH'].split(':')[0], str(runtime))
        command = toolchain.command(['/tools/bin/verilator', '--build', 'test.sv'])
        self.assertIn("-L'" + str(runtime) + "'", command[command.index('-LDFLAGS') + 1])

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

    def test_fallback_rewrites_direct_and_nested_build_commands(self):
        conflict = subprocess.CompletedProcess([], 1, '',
            'RTLD_DEEPBIND is incompatible with sanitizer runtime')
        success = subprocess.CompletedProcess([], 0, '', '')
        flags = ['-fsanitize=undefined', '-fno-sanitize-recover=all']
        commands = [
            ['/tools/bin/clang++', 'test.cc', '-fsanitize=address,undefined', '-o', 'run'],
            ['cpphdl', '--native-graph', 'test.cc', '--', '-fsanitize=address,undefined'],
        ]
        with patch.object(self.toolchain, 'probe', side_effect=[conflict, success]) as probe:
            for command in commands:
                rewritten = self.toolchain.command(command)
                self.assertEqual(rewritten,
                                 command[:command.index('-fsanitize=address,undefined')] + flags
                                 + command[command.index('-fsanitize=address,undefined') + 1:])
                self.assertIn('-fsanitize=address,undefined', command)
            self.assertEqual(probe.call_count, 2)

    def test_supported_asan_remains_enabled_in_build_commands(self):
        with patch.object(self.toolchain, 'probe', return_value=
                          subprocess.CompletedProcess([], 0, '', '')):
            command = self.toolchain.command(['clang++', '-fsanitize=address,undefined'])
            self.assertIn('-fsanitize=address,undefined', command)
            self.assertIn('-fno-sanitize-recover=all', command)

    def test_explicit_asan_never_falls_back(self):
        with patch.dict('os.environ', {'CPPHDL_TEST_SANITIZERS': 'address,undefined'}), \
             patch.object(self.toolchain, 'probe', return_value=subprocess.CompletedProcess([], 1, '',
                 'RTLD_DEEPBIND is incompatible with sanitizer runtime')) as probe:
            with self.assertRaises(RuntimeError):
                self.toolchain.sanitizers()
            probe.assert_called_once()


if __name__ == '__main__':
    unittest.main()
