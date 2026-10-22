#!/usr/bin/env python3
"""Timeouts must stop EDA grandchildren rather than leave orphan compilers."""
from pathlib import Path
import hashlib
import json
import subprocess
import sys
import tempfile
import time
import unittest
import fcntl
from unittest.mock import patch
from types import SimpleNamespace
from hardware import execute, source_dependencies
from cell_timing import run as cell_timing
import tool_process


class EDAMemoryBudget(unittest.TestCase):
    def test_heavy_tools_exclude_each_other_and_release_on_error(self):
        with tempfile.TemporaryDirectory() as temporary:
            lock_path = Path(temporary) / 'eda.lock'
            with patch.object(tool_process, 'MEMORY_LOCK', lock_path):
                for command in (['openroad', 'design.tcl'], ['python3', '/pdk/run_lvs.py']):
                    with self.assertRaisesRegex(RuntimeError, 'cancelled'):
                        with tool_process.memory_slot(command):
                            with lock_path.open('a+') as second:
                                with self.assertRaises(BlockingIOError):
                                    fcntl.flock(second, fcntl.LOCK_EX | fcntl.LOCK_NB)
                            # Ordinary compiler dependency reads can progress.
                            with tool_process.memory_slot(['c++', '-MM']):
                                pass
                            raise RuntimeError('cancelled')
                    with lock_path.open('a+') as released:
                        fcntl.flock(released, fcntl.LOCK_EX | fcntl.LOCK_NB)
                        fcntl.flock(released, fcntl.LOCK_UN)


class DependencyEvidence(unittest.TestCase):
    def test_active_macro_includes_and_spaces(self):
        with tempfile.TemporaryDirectory(prefix='cpphdl dependency test ') as temporary:
            root = Path(temporary)
            source = root / 'module.cpp'
            active = root / 'active header.hpp'
            nested = root / 'nested.hpp'
            inactive = root / 'inactive.hpp'
            active.write_text('#include "nested.hpp"\n')
            nested.write_text('// selected implementation\n')
            inactive.write_text('// unused implementation\n')
            source.write_text('#ifdef PIPELINE\n#define HEADER "active header.hpp"\n#include HEADER\n'
                              '#else\n#include "inactive.hpp"\n#endif\n')
            expected = {str(p) for p in (source, active, nested)}
            first = source_dependencies([source], root, defines=['-DPIPELINE'])
            self.assertEqual(set(first), expected)
            inactive.write_text('// changed but still unused\n')
            self.assertEqual(first, source_dependencies([source], root, defines=['-DPIPELINE']))
            self.assertEqual(set(source_dependencies([source], root)), {str(source), str(inactive)})


class TimingEvidence(unittest.TestCase):
    def test_changed_source_cannot_reuse_gate_evidence(self):
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            library = root / 'pdk/ihp-sg13g2/libs.ref/sg13g2_stdcell'
            mapped = root / 'mapped.v'
            for path in (mapped, library / 'lef/sg13g2_tech.lef', library / 'lef/sg13g2_stdcell.lef',
                         library / 'lib/sg13g2_stdcell_slow_1p08V_125C.lib'):
                path.parent.mkdir(parents=True, exist_ok=True)
                path.write_text('test input')
            source = root / 'module.hpp'
            source.write_text('new source revision')
            evidence = dict(status='pass', native_mapped_exact=True, module='ReceiveTimingLoop',
                            mapped_sha256=hashlib.sha256(mapped.read_bytes()).hexdigest(),
                            source_sha256={str(source): hashlib.sha256(b'old source revision').hexdigest()})
            (root / 'hardware_checks.json').write_text(json.dumps(evidence))
            args = SimpleNamespace(work=root, pdk=root / 'pdk', module='ReceiveTimingLoop',
                                   tools=root / 'absent-tools', period=8., repair=True, require_functional=True)
            with self.assertRaisesRegex(ValueError, 'source provenance is absent or stale'):
                cell_timing(args)
            self.assertFalse((root / 'cell_timing.tcl').exists())

    def test_stale_or_failed_functional_proof_cannot_qualify_timing(self):
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            library = root / 'pdk/ihp-sg13g2/libs.ref/sg13g2_stdcell'
            for path in (root / 'mapped.v', library / 'lef/sg13g2_tech.lef',
                         library / 'lef/sg13g2_stdcell.lef', library / 'lib/sg13g2_stdcell_slow_1p08V_125C.lib'):
                path.parent.mkdir(parents=True, exist_ok=True)
                path.write_text('deliberately nonfunctional input')
            args = SimpleNamespace(work=root, pdk=root / 'pdk', module='PipelinedReceiveFilter',
                                   tools=root / 'absent-tools', period=8., repair=True, require_functional=True)
            for evidence in ({'status': 'fail'},
                             {'status': 'pass', 'native_mapped_exact': True, 'module': args.module,
                              'mapped_sha256': 'stale'}):
                (root / 'hardware_checks.json').write_text(json.dumps(evidence))
                with self.subTest(evidence=evidence), self.assertRaisesRegex(ValueError, 'absent, failed or stale'):
                    cell_timing(args)
                self.assertFalse((root / 'cell_timing.tcl').exists(), 'timing tool launched on invalid evidence')


class HardwareRunner(unittest.TestCase):
    def assert_stopped(self, pid):
        try:
            status = (Path('/proc') / str(pid) / 'status').read_text()
        except (FileNotFoundError, ProcessLookupError):
            status = ''
        if status:
            self.assertIn('State:\tZ', status)

    def test_nonzero_exit_remains_a_failure(self):
        with self.assertRaises(subprocess.CalledProcessError):
            execute([sys.executable, '-c', 'raise SystemExit(7)'], check=True, timeout=10)

    def test_timeout_terminates_descendants(self):
        with tempfile.TemporaryDirectory() as temporary:
            pid_file = Path(temporary) / 'child.pid'
            # The simulated tool starts its own compiler. The compiler writes
            # its PID before blocking, so this checks the actual descendant.
            child = ('import os,time\nfrom pathlib import Path\n'
                     f'Path({str(pid_file)!r}).write_text(str(os.getpid()))\ntime.sleep(120)\n')
            parent = ('import subprocess,sys,time\n'
                      f'subprocess.Popen([sys.executable, "-c", {child!r}])\ntime.sleep(120)\n')
            with self.assertRaises(subprocess.TimeoutExpired):
                execute([sys.executable, '-c', parent], check=True, timeout=1)
            self.assertTrue(pid_file.is_file(), 'test tool never started its compiler')
            self.assert_stopped(pid_file.read_text())

    def test_outer_ctest_termination_cleans_eda_process_group(self):
        with tempfile.TemporaryDirectory() as temporary:
            pid_file = Path(temporary) / 'tool.pid'
            child = ('import os,time\nfrom pathlib import Path\n'
                     f'Path({str(pid_file)!r}).write_text(str(os.getpid()))\ntime.sleep(120)\n')
            wrapper = ('import sys\nfrom hardware import configure_cleanup,execute\n'
                       f'configure_cleanup()\nexecute([sys.executable,"-c",{child!r}],timeout=120)\n')
            process = subprocess.Popen([sys.executable, '-c', wrapper], cwd=Path(__file__).resolve().parent)
            try:
                deadline = time.monotonic() + 5
                while time.monotonic() < deadline and (not pid_file.exists() or not pid_file.stat().st_size):
                    time.sleep(.01)
                self.assertTrue(pid_file.is_file(), 'test runner never started its tool')
                process.terminate()
                self.assertEqual(process.wait(timeout=5), 143)
                self.assert_stopped(pid_file.read_text())
            finally:
                if process.poll() is None:
                    process.kill()
                    process.wait()


if __name__ == '__main__':
    unittest.main()
