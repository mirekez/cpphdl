"""Keep compiler runtime selection independent of the host's installed library."""
import contextlib
import io
import json
import os
from pathlib import Path
import subprocess
import sys
import tempfile
import unittest
from unittest.mock import patch

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))
import synthesize


class EmitterToolchainChecks(unittest.TestCase):
    def setUp(self):
        temporary = tempfile.TemporaryDirectory()
        self.addCleanup(temporary.cleanup)
        self.root = Path(temporary.name).resolve()
        self.library = self.root / 'compiler runtime' / 'libstdc++.so.6'
        self.library.parent.mkdir()
        self.library.touch()
        self.alias = self.root / 'runtime alias'
        self.alias.symlink_to(self.library.parent, target_is_directory=True)
        self.libraries = {
            'libstdc++.so': str(self.alias / self.library.name),
            'libgcc_s.so.1': str(self.library.parent / 'libgcc_s.so.1'),
        }
        Path(self.libraries['libgcc_s.so.1']).touch()

    def query(self, command, **kwargs):
        self.assertEqual(command[0], '/selected/c++')
        return self.libraries[command[1].split('=', 1)[1]] + '\n'

    def test_runtime_precedes_host_and_preserves_environment(self):
        with patch.dict(os.environ, {'LD_LIBRARY_PATH': '/old/runtime:/application'}, clear=True), \
                patch.object(synthesize.subprocess, 'check_output', side_effect=self.query):
            env = synthesize.emitter_environment('/selected/c++', 30)
            self.assertEqual(env['LD_LIBRARY_PATH'],
                             str(self.library.parent) + ':/old/runtime:/application')
            self.assertEqual(os.environ['LD_LIBRARY_PATH'], '/old/runtime:/application')

    def test_missing_runtime_does_not_add_current_directory(self):
        with patch.dict(os.environ, {}, clear=True), \
                patch.object(synthesize.subprocess, 'check_output', return_value='libstdc++.so\n'):
            self.assertNotIn('LD_LIBRARY_PATH', synthesize.emitter_environment('/selected/c++', 30))

    def test_distinct_gcc_runtime_directory(self):
        runtime = self.root / 'gcc runtime' / 'libgcc_s.so.1'
        runtime.parent.mkdir()
        runtime.touch()
        self.libraries['libgcc_s.so.1'] = str(runtime)
        with patch.dict(os.environ, {}, clear=True), \
                patch.object(synthesize.subprocess, 'check_output', side_effect=self.query):
            self.assertEqual(synthesize.emitter_environment('/selected/c++', 30)['LD_LIBRARY_PATH'],
                             str(self.library.parent) + ':' + str(runtime.parent))

    def run_synthesis(self, fail_emit=False):
        source = self.root / 'design.cpp'
        tool = self.root / 'cpphdl'
        source.touch()
        tool.touch()
        output = self.root / 'generated'
        commands = []

        def run(command, **kwargs):
            commands.append(command)
            if command[0] == str(output / 'emit'):
                self.assertEqual(kwargs['env']['LD_LIBRARY_PATH'],
                                 str(self.library.parent) + ':/old/runtime')
                if fail_emit:
                    kwargs['stderr'].write('example emitter diagnostic\n')
                    return subprocess.CompletedProcess(command, 1)
                (output / 'timing.json').write_text(json.dumps({
                    'keep_boxes': [], 'external_blackboxes': []}))
                (output / 'gates.json').write_text(json.dumps({
                    'modules': {'SynthTop': {'cells': {}}}}))
            else:
                self.assertIsNone(kwargs['env'])
            return subprocess.CompletedProcess(command, 0)

        argv = ['synthesize.py', '--cpphdl', str(tool), '--cxx', '/selected/c++',
                '--top', 'Top', '--output', str(output), str(source)]
        stderr = io.StringIO()
        with patch.object(sys, 'argv', argv), \
                patch.dict(os.environ, {'LD_LIBRARY_PATH': '/old/runtime'}), \
                patch.object(synthesize.shutil, 'which', return_value='/selected/c++'), \
                patch.object(synthesize.subprocess, 'check_output', side_effect=self.query), \
                patch.object(synthesize.subprocess, 'run', side_effect=run), \
                contextlib.redirect_stderr(stderr), contextlib.redirect_stdout(io.StringIO()):
            result = synthesize.main()
        self.assertEqual(len(commands), 3)
        return result, stderr.getvalue(), json.loads((output / 'manifest.json').read_text())

    def test_synthesis_uses_selected_runtime_only_for_emitter(self):
        result, stderr, manifest = self.run_synthesis()
        self.assertEqual(result, 0, stderr)
        self.assertEqual(manifest['status'], 'complete')

    def test_failed_emitter_diagnostic_reaches_caller(self):
        result, stderr, manifest = self.run_synthesis(fail_emit=True)
        self.assertEqual(result, 1)
        self.assertEqual(manifest['status'], 'failed')
        self.assertIn('example emitter diagnostic', stderr)


if __name__ == '__main__':
    unittest.main()
