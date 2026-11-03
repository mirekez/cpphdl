"""A zero-exit timing failure or missing disk cannot advance the receiver flow."""
import json
from pathlib import Path
import sys
import tempfile
from types import SimpleNamespace
import unittest
from unittest.mock import patch

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))
import receiver_continue as flow


class Continuation(unittest.TestCase):
    def setUp(self):
        tmp = tempfile.TemporaryDirectory()
        self.addCleanup(tmp.cleanup)
        self.root = Path(tmp.name)
        repair = self.root / 'repair'
        repair.mkdir()
        self.proof = dict(status='global_repair_candidate', stage='complete', margin_screen={},
                          source_sha256={'source': 'hash'}, artifact_sha256={'artifact': 'hash'})
        (repair / 'checks.json').write_text(json.dumps(self.proof))
        self.args = SimpleNamespace(repair=repair, mapped_work=self.root / 'mapped',
            constraints=self.root / 'input.sdc', output=self.root / 'finish',
            pdk=self.root / 'pdk', tools=self.root / 'tools')
        self.calls = []
        self.bad_stage = None

    def execute(self, command, **options):
        name = Path(options['stdout'].name).stem
        self.calls.append(name)
        self.assertFalse(options['memory_exclusive'])
        output = Path(command[command.index('--output') + 1])
        output.mkdir()
        statuses = dict(route='routed_timing_candidate_pass', replay='receiver_replay_pass',
                        layout='receiver_geometry_pass')
        (output / 'checks.json').write_text(json.dumps(dict(stage='complete',
            status='routed_timing_fail' if name == self.bad_stage else statuses[name])))
        return SimpleNamespace(returncode=0)

    def run_flow(self):
        with patch.object(flow, 'wait_for_producer'), patch.object(flow, 'verify_files'), \
             patch.object(flow, 'final_screen', return_value={}), \
             patch.object(flow, 'wait_for_space'), patch.object(flow, 'execute', side_effect=self.execute):
            flow.run(self.args)

    def test_all_standalone_checks_are_required(self):
        self.run_flow()
        self.assertEqual(self.calls, ['route', 'replay', 'layout'])
        d = json.loads((self.args.output / 'checks.json').read_text())
        self.assertEqual(d['status'], 'receiver_standalone_checks_pass')
        self.assertFalse(d['full_chip_qualified'])

    def test_zero_exit_failed_timing_does_not_launch_replay(self):
        self.bad_stage = 'route'
        with self.assertRaisesRegex(ValueError, 'did not pass'):
            self.run_flow()
        self.assertEqual(self.calls, ['route'])

    def test_failed_replay_does_not_launch_layout(self):
        self.bad_stage = 'replay'
        with self.assertRaisesRegex(ValueError, 'did not pass'):
            self.run_flow()
        self.assertEqual(self.calls, ['route', 'replay'])

    def test_failed_probe_never_launches_full_route(self):
        self.args.route_probe = self.root / 'probe'
        with patch.object(flow, 'wait_for_producer'), \
             patch.object(flow, 'diagnostic_inputs', side_effect=ValueError('setup timing failed')), \
             patch.object(flow, 'execute') as execute:
            with self.assertRaisesRegex(ValueError, 'setup timing failed'):
                flow.run(self.args)
        execute.assert_not_called()

    def test_completed_probe_is_passed_to_full_route(self):
        self.args.route_probe = self.root / 'probe'
        with patch.object(flow, 'diagnostic_inputs', return_value=({'input': 'hash'}, None, None)):
            self.run_flow()
        self.assertEqual(self.calls, ['route', 'replay', 'layout'])
        proof = json.loads((self.args.output / 'checks.json').read_text())
        command = proof['stages'][0]['command']
        self.assertEqual(command[command.index('--route-probe') + 1], str(self.args.route_probe))

    def test_disk_wait_requires_measured_space(self):
        with patch.object(flow.shutil, 'disk_usage', side_effect=[SimpleNamespace(free=1),
                SimpleNamespace(free=4 * 1024**3)]), patch.object(flow.time, 'sleep') as sleep:
            flow.wait_for_space(self.root)
        sleep.assert_called_once()

    def test_elapsed_time_does_not_imply_disk_space(self):
        with patch.object(flow.shutil, 'disk_usage', return_value=SimpleNamespace(free=1)), \
             self.assertRaises(TimeoutError):
            flow.wait_for_space(self.root, timeout=0)


if __name__ == '__main__':
    unittest.main()
