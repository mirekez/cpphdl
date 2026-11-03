"""A retained build cannot turn incomplete, stale or mismatched replay into a pass."""
from contextlib import nullcontext
import json
import os
from pathlib import Path
import sys
import tempfile
from types import SimpleNamespace
import unittest
from unittest.mock import patch

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))
import receiver_gate_replay as flow


class ReceiverReplay(unittest.TestCase):
    def setUp(self):
        temporary = tempfile.TemporaryDirectory()
        self.addCleanup(temporary.cleanup)
        root = Path(temporary.name)
        self.args = SimpleNamespace(route=root / 'route', mapped_work=root / 'mapped',
            output=root / 'replay', resume=False, verilator=str(root / 'verilator'), cxx=str(root / 'cxx'))
        for path in (self.args.route, self.args.mapped_work):
            path.mkdir()
        for name in ('verilator', 'verilator_bin', 'cxx'):
            (root / name).write_text('test executable')
        self.source = root / 'source.cpp'
        self.source.write_text('verified design')
        for name in ('postroute.v', 'core.odb'):
            (self.args.route / name).write_text('routed fixture')
        (self.args.route / 'core.spef').write_text('*SPEF\n' + '*D_NET n 1\n' * 40)
        (self.args.route / 'setup.rpt').write_text('worst slack max 0.2\n')
        (self.args.route / 'hold.rpt').write_text('worst slack min 0.1\n')
        (self.args.route / 'electrical.rpt').write_text('')
        for name in {'mapped.v', 'cells.v'} | {n for _, f, t in flow.CASES for n in (f, t)}:
            (self.args.mapped_work / name).write_text('verified ' + name + '\n')
        self.proof = dict(status='routed_timing_candidate_pass', stage='complete',
            routed_connectivity=dict(status='pass', top='PCSReceiver'),
            source_sha256={str(p): flow.digest(p) for p in (self.source, self.args.mapped_work / 'cells.v')},
            artifact_sha256={str(p): flow.digest(p) for p in self.args.route.iterdir()})
        self.oracle = dict(status='pass', module='PCSReceiver', native_mapped_exact=True,
            native_rtl_exact=True, mapped_sha256=flow.digest(self.args.mapped_work / 'mapped.v'),
            source_sha256={str(p): flow.digest(p) for p in self.args.mapped_work.iterdir()})
        for name in ('receiver.cpp', 'receiver_fault_checks.hpp', 'receiver_enable_checks.hpp'):
            path = flow.HERE.parent / 'tests' / name
            self.oracle['source_sha256'][str(path)] = flow.digest(path)
        self.save()
        self.calls = []
        self.bad_mode = None
        self.on_wait = lambda _: None

    def save(self):
        (self.args.route / 'checks.json').write_text(json.dumps(self.proof))
        (self.args.mapped_work / 'hardware_checks.json').write_text(json.dumps(self.oracle))

    def execute(self, command, **options):
        name = Path(options['stdout'].name).stem
        self.calls.append(name)
        if name == 'build':
            self.assertEqual(options['timeout'], 7200)
            self.assertEqual(command[command.index('-j') + 1], '1')
            executable = self.args.output / 'obj/VPCSReceiver'
            executable.parent.mkdir(exist_ok=True)
            executable.write_text('built gate model')
        else:
            mode, fixture, trace = next(case for case in flow.CASES if case[0] == name)
            self.assertEqual(options['timeout'], 900)
            self.assertEqual(command[1:-2], [] if mode == 'normal' else ['--' + mode])
            Path(command[-1]).write_bytes((self.args.mapped_work / trace).read_bytes()
                if mode != self.bad_mode else b'different\n')
        return SimpleNamespace(returncode=0)

    def run_flow(self):
        with patch.object(flow, 'memory_slot', return_value=nullcontext(True)), \
             patch.object(flow, 'execute', side_effect=self.execute), \
             patch.object(flow.time, 'sleep', side_effect=lambda seconds: self.on_wait(seconds)), \
             patch.object(flow.shutil, 'disk_usage', return_value=SimpleNamespace(free=2 * 1024**3)):
            flow.run(self.args)

    def test_all_three_replays_are_required(self):
        self.run_flow()
        self.assertEqual(self.calls, ['build', 'normal', 'faults', 'enable'])
        report = json.loads((self.args.output / 'checks.json').read_text())
        self.assertEqual(report['status'], 'receiver_replay_pass')
        self.assertTrue(report['functional_qualified'])
        self.assertFalse(report['physical_qualified'])
        self.assertFalse(report['full_chip_qualified'])

    def test_incomplete_route_stops_before_build(self):
        self.proof['status'] = 'running'
        self.save()
        with self.assertRaisesRegex(ValueError, 'must finish'):
            self.run_flow()
        self.assertEqual(self.calls, [])

    def test_waits_for_live_producer_to_publish_final_receipt(self):
        self.proof.update(status='running', pid=os.getpid())
        self.save()
        waited = []
        def finish(seconds):
            waited.append(seconds)
            self.proof['status'] = 'routed_timing_candidate_pass'
            self.save()
        self.on_wait = finish
        self.run_flow()
        self.assertEqual(waited, [.25])
        self.assertEqual(self.calls, ['build', 'normal', 'faults', 'enable'])

    def test_changed_oracle_trace_stops_before_build(self):
        (self.args.mapped_work / 'cpp_fault_trace.csv').write_text('changed oracle')
        with self.assertRaisesRegex(ValueError, 'changed replay input'):
            self.run_flow()
        self.assertEqual(self.calls, [])

    def test_unbound_route_netlist_is_rejected(self):
        self.proof['artifact_sha256'].pop(str(self.args.route / 'postroute.v'))
        self.save()
        with self.assertRaisesRegex(ValueError, 'unbound routed artifact'):
            self.run_flow()
        self.assertEqual(self.calls, [])

    def test_changed_enable_trace_fails_then_resume_reuses_only_build(self):
        self.bad_mode = 'enable'
        with self.assertRaisesRegex(ValueError, 'trajectory differs'):
            self.run_flow()
        report = json.loads((self.args.output / 'checks.json').read_text())
        self.assertFalse(report['functional_qualified'])
        self.bad_mode, self.args.resume, self.calls = None, True, []
        self.run_flow()
        self.assertEqual(self.calls, ['normal', 'faults', 'enable'])

    def test_changed_executable_cannot_resume(self):
        self.run_flow()
        (self.args.output / 'obj/VPCSReceiver').write_text('different executable')
        self.args.resume, self.calls = True, []
        with self.assertRaisesRegex(ValueError, 'executable changed'):
            self.run_flow()
        self.assertEqual(self.calls, [])

    def test_failing_extracted_electrical_limits_reject_build(self):
        p = self.args.route / 'electrical.rpt'
        p.write_text('max capacitance VIOLATED')
        self.proof['artifact_sha256'][str(p)] = flow.digest(p)
        self.save()
        with self.assertRaises(ValueError):
            self.run_flow()
        self.assertEqual(self.calls, [])


if __name__ == '__main__':
    unittest.main()
