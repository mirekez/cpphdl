"""Reject stale replay handoffs and incomplete receiver physical verification."""
from contextlib import nullcontext
import importlib.util
import json
import os
from pathlib import Path
import sys
from types import SimpleNamespace
import unittest
from unittest.mock import patch

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))
import receiver_layout as flow

spec = importlib.util.spec_from_file_location('replay_fixture', Path(__file__).with_name('receiver_gate_replay.py'))
fixture = importlib.util.module_from_spec(spec)
spec.loader.exec_module(fixture)


class ReceiverLayout(unittest.TestCase):
    def setUp(self):
        self.replay = fixture.ReceiverReplay()
        self.replay.setUp()
        self.addCleanup(self.replay.doCleanups)
        route = self.replay.args.route
        (route / 'postroute.v').write_text('module PCSReceiver(A, Y);\ninput A;\noutput Y;\nassign Y = A;\nendmodule\n')
        for name in ('core.def', 'core.cdl', 'route_drc.rpt'):
            (route / name).write_text('' if name == 'route_drc.rpt' else 'routed ' + name)
        self.replay.proof['artifact_sha256'].update({str(p): flow.digest(p) for p in route.iterdir()
                                                    if p.name != 'checks.json'})
        self.replay.save()
        self.replay.run_flow()
        root = route.parent
        self.args = SimpleNamespace(route=route, replay=self.replay.args.output,
            mapped_work=self.replay.args.mapped_work, output=root / 'layout',
            pdk=root / 'pdk', tools=root / 'tools', wait=False)
        self.calls = []
        self.fail_stage = None
        self.mutate_fill = False

    def edit_replay(self, edit):
        path = self.args.replay / 'checks.json'
        report = json.loads(path.read_text())
        edit(report)
        path.write_text(json.dumps(report))

    def execute(self, command, **options):
        name = Path(options['stdout'].name).stem
        self.calls.append(name)
        output = self.args.output
        if name == 'technology':
            (output / 'route_tech.lef').write_text('prepared LEF')
        elif name == 'stream':
            (output / 'repeater_core_unfilled.gds').write_text('streamed GDS')
        elif name == 'fill':
            (output / 'module_filled.gds').write_text('filled GDS')
        elif name == 'drc':
            self.assertIn('--antenna', command)
            self.assertEqual(command[command.index('--mp') + 1], '1')
            if self.mutate_fill:
                (output / 'module_filled.gds').write_text('changed after fill')
        return SimpleNamespace(returncode=1 if name == self.fail_stage else 0)

    def run_flow(self, drc_error=None, lvs_error=None):
        with patch.object(flow, 'memory_slot', return_value=nullcontext(True)), \
             patch.object(flow, 'physical_inputs', return_value={str(self.replay.source): flow.digest(self.replay.source)}), \
             patch.object(flow, 'execute', side_effect=self.execute), \
             patch.object(flow, 'check_drc', side_effect=drc_error, return_value={'violations': 0}), \
             patch.object(flow, 'check_lvs', side_effect=lvs_error, return_value={'matched_circuits': 1}) as lvs, \
             patch.object(flow.shutil, 'disk_usage', return_value=SimpleNamespace(free=2 * 1024**3)):
            flow.run(self.args)
            self.assertEqual(lvs.call_args.args[2], {'A', 'Y', 'VDD', 'VSS'})

    def test_exact_handoff_runs_all_physical_checks_without_gate_rebuild(self):
        self.run_flow()
        self.assertEqual(self.calls, ['technology', 'stream', 'fill', 'drc', 'lvs'])
        report = json.loads((self.args.output / 'checks.json').read_text())
        self.assertEqual(report['status'], 'receiver_geometry_pass')
        self.assertTrue(report['geometry_qualified'])
        self.assertFalse(report['postfill_timing_qualified'])
        self.assertFalse(report['full_chip_qualified'])
        self.assertIn(str(self.args.output / 'module_filled.gds'), report['artifact_sha256'])

    def test_missing_enable_trace_binding_rejects_before_stream(self):
        self.edit_replay(lambda d: d['artifact_sha256'].pop(str(self.args.replay / 'enable_trace.csv')))
        with self.assertRaisesRegex(ValueError, 'unbound.*enable'):
            self.run_flow()
        self.assertEqual(self.calls, [])

    def test_different_route_receipt_rejects_before_stream(self):
        self.edit_replay(lambda d: d['input_sha256'].pop(str(self.args.route / 'checks.json')))
        with self.assertRaisesRegex(ValueError, 'does not bind'):
            self.run_flow()
        self.assertEqual(self.calls, [])

    def test_modified_def_rejects_before_stream(self):
        (self.args.route / 'core.def').write_text('different layout')
        with self.assertRaisesRegex(ValueError, 'changed replay input'):
            self.run_flow()
        self.assertEqual(self.calls, [])

    def test_running_replay_is_not_a_pass(self):
        self.edit_replay(lambda d: d.update(status='running'))
        with self.assertRaisesRegex(ValueError, 'completed exact'):
            self.run_flow()
        self.assertEqual(self.calls, [])

    def test_changed_executable_is_rejected(self):
        (self.args.replay / 'obj/VPCSReceiver').write_text('replacement executable')
        with self.assertRaisesRegex(ValueError, 'executable changed'):
            self.run_flow()

    def test_checker_exit_failure_is_not_accepted(self):
        self.fail_stage = 'drc'
        with self.assertRaisesRegex(ValueError, 'stage failed: drc'):
            self.run_flow()
        self.assertNotIn('lvs', self.calls)

    def test_incomplete_drc_report_rejects_zero_tool_exit(self):
        with self.assertRaisesRegex(ValueError, 'DRC incomplete'):
            self.run_flow(drc_error=ValueError('DRC incomplete'))
        self.assertNotIn('lvs', self.calls)

    def test_lvs_mismatch_prevents_geometry_pass(self):
        with self.assertRaisesRegex(ValueError, 'LVS mismatch'):
            self.run_flow(lvs_error=ValueError('LVS mismatch'))
        report = json.loads((self.args.output / 'checks.json').read_text())
        self.assertEqual(report['status'], 'fail')
        self.assertFalse(report['geometry_qualified'])

    def test_drc_and_lvs_must_use_identical_gds(self):
        self.mutate_fill = True
        with self.assertRaisesRegex(ValueError, 'changed replay input'):
            self.run_flow()
        self.assertNotIn('lvs', self.calls)

    def test_waits_outside_slot_then_revalidates(self):
        self.args.wait = True
        events = []
        with patch.object(flow, 'wait_for_producer', side_effect=lambda *a: events.append(a[1])):
            self.run_flow()
        self.assertEqual(events, ['routed_timing_candidate_pass', 'receiver_replay_pass'])

    def test_dependency_wait_rejects_failed_run(self):
        self.edit_replay(lambda d: d.update(status='fail'))
        with self.assertRaisesRegex(ValueError, 'upstream receiver run failed'):
            flow.wait_for_producer(self.args.replay, 'receiver_replay_pass', 1)

    def test_dependency_wait_rejects_dead_producer(self):
        self.edit_replay(lambda d: d.update(status='running', pid=123456))
        with patch.object(flow.os, 'kill', side_effect=ProcessLookupError), \
             self.assertRaisesRegex(ValueError, 'producer stopped'):
            flow.wait_for_producer(self.args.replay, 'receiver_replay_pass', 1)

    def test_dependency_wait_accepts_only_final_publication(self):
        self.edit_replay(lambda d: d.update(status='running', pid=os.getpid()))
        def finish(_):
            self.edit_replay(lambda d: d.update(status='receiver_replay_pass', stage='complete'))
        with patch.object(flow.time, 'sleep', side_effect=finish) as sleep:
            flow.wait_for_producer(self.args.replay, 'receiver_replay_pass', 30)
        sleep.assert_called_once()


if __name__ == '__main__':
    unittest.main()
