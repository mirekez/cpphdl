"""Absolute acquisition checks must reject a common error in both solvers."""
import math
import csv
import json
from pathlib import Path
from types import SimpleNamespace
import tempfile
import unittest
from programmable_receive_frontend import contract, verify, compare, digest, SOURCE, HEADER, WARMUP


def ideal_rows():
    rows = []
    symbols = (0, 1, -1, 2, -2, 0, 2, 1, -2, -1, 0, 1, 2, -1, -2, 0)
    for symbol, value in enumerate(symbols):
        differential = .09 * value
        gain_input = 1.65 + 1.25 * differential
        gain_output = 1.65 - .8 * (gain_input - 1.65)
        for sample in range(40):
            time = WARMUP + (8 * symbol + 7 + sample * .02) * 1e-9
            rows.append([time, 1.65 + differential / 2, 1.65 - differential / 2,
                         gain_input, gain_output, gain_output + .003, 0, 3.3, 1.2, 1.65])
    return rows


class AcquisitionContract(unittest.TestCase):
    def test_valid_loaded_transfer(self):
        verify(contract(ideal_rows(), 4))

    def test_acquisition_error_cannot_cancel_in_aperture_average(self):
        rows = ideal_rows()
        for index, row in enumerate(rows):
            row[5] = row[4] + (.021 if index & 1 else -.021)
        with self.assertRaisesRegex(ValueError, 'acquisition'):
            verify(contract(rows, 4))

    def test_wrong_polarity_is_rejected(self):
        rows = ideal_rows()
        for row in rows:
            row[4] = 3.3 - row[4]
        with self.assertRaisesRegex(ValueError, 'gain'):
            verify(contract(rows, 4))

    def test_track_stuck_on_and_rail_collapse_are_rejected(self):
        for column, value, diagnosis in [(6, 3.3, 'track_closed'), (7, 3.0, 'analog_supply'),
                                         (8, 1.0, 'logic_supply'), (4, 3.3, 'headroom')]:
            rows = ideal_rows()
            rows[0][column] = value
            with self.subTest(diagnosis=diagnosis), self.assertRaisesRegex(ValueError, diagnosis):
                verify(contract(rows, 4))

    def test_missing_aperture_and_zero_excitation_are_rejected(self):
        with self.assertRaisesRegex(ValueError, 'aperture'):
            contract(ideal_rows()[40:], 4)
        rows = ideal_rows()
        for row in rows:
            row[3] = row[9]
        with self.assertRaisesRegex(ValueError, 'stimulus'):
            contract(rows, 4)

    def test_gain_dependent_termination_is_rejected(self):
        rows = ideal_rows()
        for row in rows:
            differential = row[1] - row[2]
            row[1], row[2] = 1.65 + differential, 1.65 - differential
        with self.assertRaisesRegex(ValueError, 'fixed_termination'):
            verify(contract(rows, 4))

    def test_comparison_reads_serialized_paths_and_rechecks_both_flows(self):
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            for flow in ('cpp', 'veriloga'):
                directory = root / flow
                directory.mkdir()
                waveform = directory / 'trace.txt'
                with waveform.open('w') as output:
                    if flow == 'cpp':
                        writer = csv.writer(output)
                        writer.writerow(HEADER)
                        writer.writerows(ideal_rows())
                    else:
                        output.write('time ' + ' '.join(HEADER[1:]) + '\n')
                        for row in ideal_rows():
                            output.write(' '.join(map(str, row)) + '\n')
                report = dict(status='pass', source_sha256={str(SOURCE): digest(SOURCE)},
                    cases={'4': dict(waveform=str(waveform), waveform_sha256=digest(waveform))})
                (directory / 'checks.json').write_text(json.dumps(report))
            args = SimpleNamespace(work=root, codes=[4])
            compare(args)
            self.assertEqual(json.loads((root / 'comparison.json').read_text())['status'], 'pass')
            # Missing provenance must invalidate a previous comparison result.
            for flow in ('cpp', 'veriloga'):
                path = root / flow / 'checks.json'
                report = json.loads(path.read_text())
                report['source_sha256'] = {}
                path.write_text(json.dumps(report))
            with self.assertRaisesRegex(ValueError, 'provenance'):
                compare(args)
            self.assertFalse((root / 'comparison.json').exists())

    def test_nonfinite_samples_are_rejected(self):
        rows = ideal_rows()
        rows[0][5] = math.nan
        with self.assertRaises(ValueError):
            contract(rows, 4)


if __name__ == '__main__':
    unittest.main()
