"""Reject electrical or topology failures before spending time on detailed routing."""
from contextlib import nullcontext
import json
from pathlib import Path
from types import SimpleNamespace
import sys
import tempfile
import unittest
from unittest.mock import patch

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))
import resume_receiver_route as flow

NETLIST = '''module PCSReceiver(a, b, y);
input a;
input b;
output y;
sg13g2_nand2_1 gate(.A(a), .B(b), .Y(y));
endmodule
'''


class RouteResume(unittest.TestCase):
    def setUp(self):
        temporary = tempfile.TemporaryDirectory()
        self.addCleanup(temporary.cleanup)
        root = Path(temporary.name)
        self.args = SimpleNamespace(origin=root / 'old', output=root / 'new',
            constraints=root / 'input.sdc', pdk=root / 'pdk', tools=root / 'tools')
        self.args.origin.mkdir()
        (self.args.origin / 'global.odb').write_bytes(b'checkpoint')
        (self.args.origin / 'global.v').write_text(NETLIST)
        files = [self.args.constraints, self.args.tools / 'bin/openroad',
                 self.args.tools / 'root/usr/bin/openroad', self.args.tools / 'rcx_patterns.rules']
        files += [self.args.pdk / ('ihp-sg13g2/libs.ref/sg13g2_stdcell/lib/sg13g2_stdcell_' + c + '.lib')
                  for c in ('slow_1p08V_125C', 'fast_1p32V_m40C')]
        for p in files:
            p.parent.mkdir(parents=True, exist_ok=True)
            p.write_text('fixture input')
        files.append(flow.HERE / 'receiver_local_route.tcl')
        proof = dict(stages=[dict(name='global', returncode=0)],
            source_sha256={str(p): flow.digest(p) for p in files})
        for name in ('seed_connectivity', 'buffer_connectivity', 'filler_connectivity', 'placement_connectivity'):
            proof[name] = {'status': 'pass'}
        (self.args.origin / 'checks.json').write_text(json.dumps(proof))
        self.called = []
        self.setup, self.hold, self.electrical = '0.2', '0.1', ''
        self.netlist = NETLIST

    def execute(self, command, **options):
        self.assertEqual(options['env']['RECEIVER_ROUTE_PROBE'], '0')
        stage = Path(options['stdout'].name).stem
        self.called.append(stage)
        w = self.args.output
        if stage == 'validate':
            (w / 'global.v').write_text(self.netlist)
            (w / 'global_setup.rpt').write_text('worst slack max ' + self.setup + '\n')
            (w / 'global_hold.rpt').write_text('worst slack min ' + self.hold + '\n')
            (w / 'global_electrical.rpt').write_text(self.electrical)
        elif stage == 'detail':
            (w / 'route_drc.rpt').write_text('')
        elif stage == 'extract':
            (w / 'postroute.v').write_text(NETLIST)
            (w / 'setup.rpt').write_text('worst slack max 0.2\n')
            (w / 'hold.rpt').write_text('worst slack min 0.1\n')
            (w / 'electrical.rpt').write_text('')
            (w / 'core.spef').write_text('*SPEF\n' + '*D_NET n 1\n' * 40)
        return SimpleNamespace(returncode=0)

    def run_flow(self):
        with patch.object(flow, 'memory_slot', return_value=nullcontext(True)), \
             patch.object(flow, 'execute', side_effect=self.execute), \
             patch.object(flow.shutil, 'disk_usage', return_value=SimpleNamespace(free=2 * 1024**3)):
            flow.run(self.args)

    def test_positive_preflight_runs_fresh_route_and_extraction(self):
        # A diagnostic environment must never truncate production routing.
        with patch.dict('os.environ', {'RECEIVER_ROUTE_PROBE': '1'}):
            self.run_flow()
        self.assertEqual(self.called, ['validate', 'detail', 'extract'])
        report = json.loads((self.args.output / 'checks.json').read_text())
        self.assertEqual(report['status'], 'routed_timing_candidate_pass')
        self.assertFalse(report['full_chip_qualified'])
        self.assertFalse(report['physical_qualified'])

    def test_slew_failure_stops_before_route(self):
        self.electrical = 'max slew gate/Y VIOLATED'
        with self.assertRaisesRegex(ValueError, 'electrical limits'):
            self.run_flow()
        self.assertEqual(self.called, ['validate'])

    def test_negative_setup_stops_before_route(self):
        self.setup = '-0.001'
        with self.assertRaisesRegex(ValueError, 'setup/hold'):
            self.run_flow()
        self.assertEqual(self.called, ['validate'])

    def test_changed_checkpoint_connectivity_rejected(self):
        self.netlist = NETLIST.replace('.A(a)', '.A(b)')
        with self.assertRaisesRegex(ValueError, 'connectivity'):
            self.run_flow()
        self.assertEqual(self.called, ['validate'])

    def test_missing_timing_rejected(self):
        self.setup = 'nan'
        with self.assertRaisesRegex(ValueError, 'finite slack'):
            self.run_flow()
        self.assertEqual(self.called, ['validate'])


class DiagnosticRouteResume(RouteResume):
    def setUp(self):
        super().setUp()
        origin = self.args.origin
        (origin / 'global.odb').rename(origin / 'candidate.odb')
        (origin / 'global.v').rename(origin / 'after.v')
        self.args.constraints.rename(origin / 'candidate.sdc')
        old = str(self.args.constraints)
        self.args.constraints = origin / 'candidate.sdc'
        self.args.route_probe = origin.parent / 'probe'
        self.args.route_probe.mkdir()
        cells = origin / 'cells.v'
        cells.write_text('fixture models')
        source = json.loads((origin / 'checks.json').read_text())['source_sha256']
        del source[old]
        source[str(self.args.constraints)] = flow.digest(self.args.constraints)
        source[str(cells)] = flow.digest(cells)
        artifacts = {str(origin / n): flow.digest(origin / n)
                     for n in ('candidate.odb', 'candidate.sdc', 'after.v')}
        proof = dict(status='diagnostic_complete', cell_models=str(cells), source_sha256=source,
            artifact_sha256=artifacts, rounds=[dict(name='after', electrical_pass=True,
                setup_ns=.2, hold_ns=.1, connectivity=dict(status='pass', top='PCSReceiver'))])
        (origin / 'checks.json').write_text(json.dumps(proof))
        probe = self.args.route_probe
        for name, content in {
               'postroute.v': NETLIST, 'setup.rpt': 'worst slack max 0.2\n',
               'hold.rpt': 'worst slack min 0.1\n', 'electrical.rpt': '',
               'core.spef': '*SPEF\n' + '*D_NET n 1\n' * 40}.items():
            (probe / name).write_text(content)
        self.probe_proof = dict(status='early_route_diagnostic_complete', stage='complete',
            detail_end_iteration=1, setup_ns=.2, hold_ns=.1, electrical_pass=True,
            connectivity=dict(status='pass', top='PCSReceiver'),
            source_sha256={**source, **artifacts,
                           str(origin / 'checks.json'): flow.digest(origin / 'checks.json')},
            artifact_sha256={str(p): flow.digest(p) for p in probe.iterdir()})
        self.save_probe()

    def save_probe(self):
        (self.args.route_probe / 'checks.json').write_text(json.dumps(self.probe_proof))

    def test_incomplete_probe_rejected(self):
        self.probe_proof['status'] = 'running'
        self.save_probe()
        with self.assertRaisesRegex(ValueError, 'completed early-route'):
            self.run_flow()
        self.assertFalse(self.called)

    def test_negative_extracted_setup_rejected_even_with_pass_label(self):
        path = self.args.route_probe / 'setup.rpt'
        path.write_text('worst slack max -0.01\n')
        self.probe_proof['artifact_sha256'][str(path)] = flow.digest(path)
        self.probe_proof['setup_ns'] = -.01
        self.save_probe()
        with self.assertRaisesRegex(ValueError, 'setup timing failed'):
            self.run_flow()
        self.assertFalse(self.called)

    def test_changed_probe_report_rejected(self):
        (self.args.route_probe / 'electrical.rpt').write_text('VIOLATED')
        with self.assertRaisesRegex(ValueError, 'changed diagnostic input'):
            self.run_flow()
        self.assertFalse(self.called)

    def test_probe_from_other_candidate_rejected(self):
        self.probe_proof['source_sha256'].pop(str(self.args.origin / 'candidate.odb'))
        self.save_probe()
        with self.assertRaisesRegex(ValueError, 'did not measure this'):
            self.run_flow()
        self.assertFalse(self.called)

    def test_changed_probe_endpoint_rejected(self):
        path = self.args.route_probe / 'postroute.v'
        path.write_text(NETLIST.replace('.A(a)', '.A(b)'))
        self.probe_proof['artifact_sha256'][str(path)] = flow.digest(path)
        self.save_probe()
        with self.assertRaisesRegex(ValueError, 'connectivity'):
            self.run_flow()
        self.assertFalse(self.called)

    def test_probe_required_for_diagnostic(self):
        self.args.route_probe = None
        with self.assertRaisesRegex(ValueError, 'requires an extracted route probe'):
            self.run_flow()
        self.assertFalse(self.called)

    def test_changed_global_timing_stops_before_route(self):
        self.setup = '0.15'
        with self.assertRaisesRegex(ValueError, 'checkpoint timing changed'):
            self.run_flow()
        self.assertEqual(self.called, ['validate'])


if __name__ == '__main__':
    unittest.main()
