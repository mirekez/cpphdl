#!/usr/bin/env python3
"""Timeouts must stop EDA grandchildren rather than leave orphan compilers."""
from pathlib import Path
import hashlib
import json
import os
import shlex
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
import hardware
import composed_hardware
import pma_test_pattern
import receive_gain_control_cells


class CompilerRuntimeSelection(unittest.TestCase):
    def assert_runtime(self, commands, compiler, library, verilator, module):
        builds = [(command, env) for command, env in commands if command[0] == str(verilator)]
        self.assertEqual(len(builds), 1)
        command, env = builds[0]
        flags = command[command.index('-MAKEFLAGS') + 1]
        self.assertIn('CXX=' + compiler, shlex.split(flags))
        self.assertIn('LINK=' + compiler, shlex.split(flags))
        self.assertIn('-L' + str(library.parent), shlex.split(command[command.index('-LDFLAGS') + 1]))
        self.assertIn(str(library.parent), env['LD_LIBRARY_PATH'].split(':'))
        self.assertIn('-std=c++17', command[command.index('-CFLAGS') + 1])
        simulations = [(command, env) for command, env in commands if Path(command[0]).name == 'V' + module]
        self.assertEqual(len(simulations), 1)
        self.assertIn(str(library.parent), simulations[0][1]['LD_LIBRARY_PATH'].split(':'))

    def check_runner(self, runner, case, module, mapped):
        with tempfile.TemporaryDirectory(prefix='compiler runtime ') as temporary:
            root = Path(temporary)
            compiler = str(root / 'compiler prefix/bin/clang++')
            library = root / 'runtime libraries/libstdc++.so'
            library.parent.mkdir()
            library.write_text('compiler runtime probe fixture')
            args = SimpleNamespace(case=case, work=root / 'work', cxx=compiler,
                                   converter=root / 'cpphdl', native=root / 'native',
                                   verilator=root / 'verilator',
                                   tools=root / 'tools' if mapped else None, pdk=root / 'pdk')
            commands = []
            environment = os.environ.copy()

            def execute(command, **options):
                command = list(map(str, command))
                commands.append((command, options.get('env')))
                if command[0] == 'python3':
                    Path(command[-1]).write_text('reference fixture\n')
                elif command[0] == str(args.native) or Path(command[0]).name == 'V' + module:
                    Path(command[-1]).write_text('0,1\n')
                elif Path(command[0]).name == 'yosys':
                    (args.work / 'mapped.v').write_text('mapped gate fixture\n')
                    (args.work / 'mapped.json').write_text(json.dumps({'modules': {module: {
                        'cells': {'inv': {'type': 'sg13g2_inv_1'}}}}}))
                return subprocess.CompletedProcess(command, 0)

            with patch.object(runner, 'source_dependencies', return_value={}), \
                 patch.object(runner, 'execute', side_effect=execute), \
                 patch('subprocess.check_output', return_value=str(library)):
                runner.run(args)

            builds = [(command, env) for command, env in commands if command[0] == str(args.verilator)]
            self.assertEqual(len(builds), 3 if mapped else 1)
            for command, env in builds:
                flags = ' '.join(command[index + 1] for index, arg in enumerate(command) if arg == '-MAKEFLAGS')
                self.assertIn('CXX=' + compiler, shlex.split(flags))
                self.assertIn('LINK=' + compiler, shlex.split(flags))
                self.assertIn('-L' + str(library.parent), shlex.split(command[command.index('-LDFLAGS') + 1]))
                self.assertIn(str(library.parent), env['LD_LIBRARY_PATH'].split(':'))
                self.assertNotIn('-std=c++20', command)
            if mapped:
                flags = ' '.join(builds[-1][0][index + 1] for index, arg in enumerate(builds[-1][0]) if arg == '-MAKEFLAGS')
                self.assertIn('OPT_FAST=-O0', shlex.split(flags))
                self.assertIn('OPT_SLOW=-O0', shlex.split(flags))
            simulations = [(command, env) for command, env in commands
                           if command[0] == str(args.native) or Path(command[0]).name == 'V' + module]
            self.assertEqual(len(simulations), 4 if mapped else 2)
            for command, env in simulations:
                self.assertIn(str(library.parent), env['LD_LIBRARY_PATH'].split(':'))
            self.assertEqual(dict(os.environ), environment)

    def test_rtl_frontend_and_mapped_builds_use_selected_runtime(self):
        self.check_runner(hardware, 'filter_datapath', 'ReceiveFilter', mapped=True)

    def test_composed_build_uses_selected_runtime(self):
        self.check_runner(composed_hardware, 'normalizer', 'ReceiveSampleNormalizer', mapped=False)

    def test_pattern_cpp_rtl_and_mapped_use_selected_runtime(self):
        for flow in ('cpp', 'rtl', 'mapped'):
            with self.subTest(flow=flow), tempfile.TemporaryDirectory(prefix='pattern runtime ') as temporary:
                root = Path(temporary)
                args = SimpleNamespace(flow=flow, work=root / 'work', cxx=str(root / 'compiler/bin/clang++'),
                                       bench=root / 'bench', converter=root / 'cpphdl',
                                       verilator=None if flow == 'cpp' else root / 'verilator',
                                       tools=root / 'tools', pdk=root / 'pdk')
                library = root / 'runtime libraries/libstdc++.so'
                inputs = [library, args.bench, args.converter,
                          args.pdk / 'ihp-sg13g2/libs.ref/sg13g2_stdcell/lib/sg13g2_stdcell_typ_1p20V_25C.lib']
                inputs += [args.tools / name for name in ('bin/yosys', 'bin/sv2v', 'bin/abc',
                           'root/usr/bin/yosys', 'sv2v-Linux/sv2v', 'root/usr/bin/berkeley-abc')]
                if args.verilator:
                    inputs.append(args.verilator)
                for path in inputs:
                    path.parent.mkdir(parents=True, exist_ok=True)
                    path.write_text('toolchain fixture\n')
                commands = []
                environment = os.environ.copy()

                def execute(command, **options):
                    command = list(map(str, command))
                    commands.append((command, options.get('env')))
                    if command[0] == str(args.bench) or Path(command[0]).name == 'VPMATestPattern':
                        Path(command[-1]).write_text('0,1\n')
                    elif Path(command[0]).name == 'yosys':
                        run = next(args.work.glob('run-*'))
                        (run / 'mapped.json').write_text(json.dumps({'modules': {'PMATestPattern': {
                            'cells': {'inv': {'type': 'sg13g2_inv_1'}}}}}))
                    return subprocess.CompletedProcess(command, 0)

                def vectors(path):
                    path.write_text('reference fixture\n')
                    return 1

                with patch.object(pma_test_pattern, 'source_dependencies', return_value={}), \
                     patch.object(pma_test_pattern, 'execute', side_effect=execute), \
                     patch('subprocess.check_output', return_value=str(library)):
                    pma_test_pattern.run(args, generate_vectors=vectors)
                if flow != 'cpp':
                    self.assert_runtime(commands, args.cxx, library, args.verilator, 'PMATestPattern')
                else:
                    self.assertEqual(len(commands), 1)
                    self.assertIn(str(library.parent), commands[0][1]['LD_LIBRARY_PATH'].split(':'))
                self.assertEqual(dict(os.environ), environment)

    def test_repaired_cells_use_selected_runtime(self):
        with tempfile.TemporaryDirectory(prefix='repaired runtime ') as temporary:
            root = Path(temporary)
            module = 'ReceiveAnalogGainControl'
            mapped = root / 'mapped'
            mapped.mkdir()
            for name in ('mapped.v', 'cells.v', 'vectors.txt', 'native.csv'):
                (mapped / name).write_text('0,1\n')
            (mapped / 'mapped.json').write_text(json.dumps({'modules': {module: {'attributes': {}}}}))
            source = Path(receive_gain_control_cells.__file__).resolve()
            evidence = dict(status='pass', flow='mapped', native_hardware_exact=True, clock_hz=250000000,
                            checked_clocks=200000, run_directory=str(mapped), artifact_sha256={},
                            source_sha256={str(source.with_name(name)): hashlib.sha256(
                                source.with_name(name).read_bytes()).hexdigest()
                                for name in ('receive_gain_control.cpp', 'receive_gain_control_reference.py')})
            proof = root / 'proof.json'
            proof.write_text(json.dumps(evidence))
            args = SimpleNamespace(module=module, work=root / 'work', cxx=str(root / 'compiler/bin/clang++'),
                                   verilator=root / 'verilator', tools=root / 'tools', pdk=root / 'pdk', mapped_proof=proof)
            library = root / 'runtime libraries/libstdc++.so'
            for path in (library, Path(args.cxx), args.verilator, root / 'verilator_bin',
                         args.tools / 'bin/openroad', args.tools / 'root/usr/bin/openroad',
                         args.pdk / 'ihp-sg13g2/libs.ref/sg13g2_stdcell/lib/sg13g2_stdcell_typ_1p20V_25C.lib'):
                path.parent.mkdir(parents=True, exist_ok=True)
                path.write_text('toolchain fixture\n')
            commands = []

            def timing(options):
                (options.work / 'resized.v').write_text('  sg13g2_inv_1 gate ();\n')
                (options.work / 'cell_timing.log').write_text('timing fixture\n')
                (options.work / 'cell_timing.json').write_text(json.dumps({'input_sha256': {}, 'setup_slack_ns': 0.1}))
                return 0

            def execute(command, **options):
                command = list(map(str, command))
                commands.append((command, options.get('env')))
                if Path(command[0]).name == 'V' + module:
                    Path(command[-1]).write_text('0,1\n')
                return subprocess.CompletedProcess(command, 0)

            with patch.object(receive_gain_control_cells.cell_timing, 'run', side_effect=timing), \
                 patch.object(receive_gain_control_cells, 'execute', side_effect=execute), \
                 patch.object(receive_gain_control_cells, 'library_areas', return_value={'sg13g2_inv_1': 1.0}), \
                 patch('subprocess.check_output', return_value=str(library)):
                receive_gain_control_cells.run(args)
            self.assert_runtime(commands, args.cxx, library, args.verilator, module)


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
