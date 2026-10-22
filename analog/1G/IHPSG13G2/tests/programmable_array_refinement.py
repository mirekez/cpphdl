"""A convergence report cannot replace the two independent electrical checks."""
import importlib.util
import gzip
import hashlib
import json
from pathlib import Path
import tempfile
from types import SimpleNamespace
import unittest
from unittest.mock import patch
from programmable_receive_array_contract import ideal_rows
from programmable_receive_array import HEADER, WARMUP

spec = importlib.util.spec_from_file_location('array_process_runner', Path(__file__).with_name('programmable_array_process.py'))
runner = importlib.util.module_from_spec(spec)
spec.loader.exec_module(runner)


class Refinement(unittest.TestCase):
    def test_failed_electrical_case_keeps_measurements_and_original_trace(self):
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            models = root / 'pdk/ihp-sg13g2/libs.tech/ngspice'
            models.mkdir(parents=True)
            (models / '.spiceinit').write_text('')
            for name in ('bench', 'ngspice'):
                (root / name).write_text('test tool placeholder')
            args = SimpleNamespace(work=root / 'result', pdk=root / 'pdk', bench=root / 'bench',
                ngspice=root / 'ngspice', cxx='unused', corner='mos_tt', temperature=27,
                warmup_step=1e-9, codes=[4], fixture_source=runner.SOURCE.with_name('programmable_receive_array.cpp'), initialization='uic')
            rows = ideal_rows()
            for row in rows:
                row[38] += .021

            def execute(command, **kwargs):
                if command[0] == str(args.bench):
                    case = Path(command[1])
                    (case / 'design.va').write_text('fixture')
                    (case / 'bench.cir').write_text('fixture')
                else:
                    with (kwargs['cwd'] / 'spice.dat').open('w') as output:
                        output.write('time ' + ' '.join(HEADER[1:]) + '\n')
                        output.writelines(' '.join(map(str, row)) + '\n' for row in rows)

            grid = dict(start_s=WARMUP, stop_s=WARMUP + 256e-9,
                        measurement_maximum_step_s=20e-12, warmup_maximum_step_s=1e-9)
            with patch.object(runner, 'execute', execute), \
                 patch.object(runner, 'source_dependencies', return_value={}), \
                 patch.object(runner, 'process_deck', return_value=('fixture', [])), \
                 patch.object(runner, 'size_mos_instances', return_value=('fixture', [])), \
                 patch.object(runner, 'windowed_deck', side_effect=lambda text: text), \
                 patch.object(runner, 'windowed_transient', return_value=('fixture', grid)):
                with self.assertRaisesRegex(ValueError, 'acquisition'):
                    runner.run(args)
            report = json.loads((args.work / 'checks.json').read_text())
            self.assertEqual(report['status'], 'fail')
            case = report['cases']['4']
            self.assertFalse(case['checks']['acquisition'])
            raw = Path(case['waveform'])
            self.assertTrue(raw.is_file())
            self.assertEqual(raw.suffix, '.gz')
            with gzip.open(raw, 'rt') as reconstructed:
                self.assertEqual(len(reconstructed.readlines()), len(rows) + 1)
            self.assertEqual(case['waveform_archive']['original_sha256'],
                             hashlib.sha256(gzip.decompress(raw.read_bytes())).hexdigest())
            self.assertEqual(case['artifact_sha256'][str(raw)], runner.digest(raw))

    def fixture(self, root):
        roots = [root / 'coarse', root / 'fine']
        for directory, step in zip(roots, (1e-9, .5e-9)):
            case = directory / '4'
            case.mkdir(parents=True)
            grid = dict(start_s=WARMUP, stop_s=WARMUP + 256e-9,
                        measurement_maximum_step_s=20e-12, warmup_maximum_step_s=step, initialization='uic')
            (case / 'timing-grid.json').write_text(json.dumps(grid))
            with (case / 'spice.dat').open('w') as output:
                output.write('time ' + ' '.join(HEADER[1:]) + '\n')
                for row in ideal_rows():
                    output.write(' '.join(map(str, row)) + '\n')
            raw, archive = runner.archive_trace(case / 'spice.dat')
            report = dict(status='pass', corner='mos_tt', temperature_C=27, initialization='uic',
                fixture_source=str(runner.SOURCE.with_name('programmable_receive_array.cpp')),
                run_directory=str(directory), warmup_maximum_step_s=step,
                source_sha256={str(runner.SOURCE): runner.digest(runner.SOURCE)},
                cases={'4': dict(waveform=str(raw), artifact_sha256={str(path): runner.digest(path) for path in case.iterdir()})})
            (directory / 'checks.json').write_text(json.dumps(report))
        return SimpleNamespace(compare=roots, output=root / 'comparison.json', codes=[4])

    def test_valid_independent_flows(self):
        with tempfile.TemporaryDirectory() as temporary:
            args = self.fixture(Path(temporary))
            runner.compare(args)
            self.assertEqual(json.loads(args.output.read_text())['status'], 'pass')

    def test_failed_flow_and_missing_provenance_invalidate_previous_success(self):
        for fault in ('failed', 'unproven', 'same_step', 'initialization'):
            with self.subTest(fault=fault), tempfile.TemporaryDirectory() as temporary:
                args = self.fixture(Path(temporary))
                args.output.write_text('{"status":"pass"}')
                path = args.compare[1] / 'checks.json'
                report = json.loads(path.read_text())
                if fault == 'failed':
                    report['status'] = 'fail'
                elif fault == 'unproven':
                    for directory in args.compare:
                        other = json.loads((directory / 'checks.json').read_text())
                        other['source_sha256'] = {}
                        (directory / 'checks.json').write_text(json.dumps(other))
                    report['source_sha256'] = {}
                elif fault == 'initialization':
                    report['initialization'] = 'bias'
                else:
                    report['warmup_maximum_step_s'] = 1e-9
                path.write_text(json.dumps(report))
                with self.assertRaises(ValueError):
                    runner.compare(args)
                self.assertFalse(args.output.exists())

    def test_bad_acquisition_is_rechecked_even_with_matching_artifact_hash(self):
        with tempfile.TemporaryDirectory() as temporary:
            args = self.fixture(Path(temporary))
            path = args.compare[1] / '4/spice.dat.gz'
            with gzip.open(path, 'rt') as source:
                lines = source.read().splitlines()
            row = lines[151].split()
            row[8] = str(float(row[8]) + .021)
            lines[151] = ' '.join(row)
            with gzip.open(path, 'wt') as destination:
                destination.write('\n'.join(lines) + '\n')
            report_path = args.compare[1] / 'checks.json'
            report = json.loads(report_path.read_text())
            report['cases']['4']['artifact_sha256'][str(path)] = runner.digest(path)
            report_path.write_text(json.dumps(report))
            with self.assertRaisesRegex(ValueError, 'acquisition'):
                runner.compare(args)

    def test_close_flow_reports_cannot_hide_actual_step_mismatch(self):
        with tempfile.TemporaryDirectory() as temporary:
            args = self.fixture(Path(temporary))
            path = args.compare[1] / '4/timing-grid.json'
            grid = json.loads(path.read_text())
            grid['warmup_maximum_step_s'] = 1e-9
            path.write_text(json.dumps(grid))
            report_path = args.compare[1] / 'checks.json'
            report = json.loads(report_path.read_text())
            report['cases']['4']['artifact_sha256'][str(path)] = runner.digest(path)
            report_path.write_text(json.dumps(report))
            with self.assertRaisesRegex(ValueError, 'actual integration grid'):
                runner.compare(args)


if __name__ == '__main__':
    unittest.main()
