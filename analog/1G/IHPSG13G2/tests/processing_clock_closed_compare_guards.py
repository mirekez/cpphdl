"""Closed-loop comparison must reject missing, stale and changed evidence."""
import gzip
import hashlib
import json
from pathlib import Path
import tempfile
import unittest

from processing_clock_closed_compare import compare, read_trace, trim_fraction, control_residual, HEADER, LIMITS


class ClosedComparisonEvidence(unittest.TestCase):
    def test_trim_prediction_matches_independent_passive_ladders(self):
        # Solve the actual two 8-bit R/2R networks and their mixing node using
        # nodal conductances. No collapsed DAC weight formula is used here.
        for trim in (0, 255, 256, 32768, 65535, 31295, 31423):
            matrix = [[0.0] * 18 for _ in range(17)]

            def resistor(a, b, resistance):
                g = 1 / resistance
                matrix[a][a] += g
                matrix[b][b] += g
                matrix[a][b] -= g
                matrix[b][a] -= g

            def rail(a, voltage, resistance):
                matrix[a][a] += 1 / resistance
                matrix[a][-1] += voltage / resistance

            for ladder, code in enumerate((trim >> 8, trim & 255)):
                for bit in range(8):
                    node = 8 * ladder + bit
                    rail(node, (code >> bit) & 1, 20000)
                    if bit:
                        resistor(node, node - 1, 10000)
                rail(8 * ladder, 0, 20000)
            resistor(7, 16, 10000)
            resistor(15, 16, 5090000)
            for column in range(17):
                pivot = matrix[column][column]
                matrix[column] = [x / pivot for x in matrix[column]]
                for row in range(17):
                    if row != column:
                        factor = matrix[row][column]
                        matrix[row] = [x - factor * y for x, y in zip(matrix[row], matrix[column])]
            self.assertAlmostEqual(matrix[16][-1], trim_fraction(trim), places=12)

    def test_code_adjustment_does_not_hide_voltage_error(self):
        supply = 1.2
        control = supply * trim_fraction(31423)
        self.assertAlmostEqual(0, control_residual(control, supply, 31423), places=12)
        self.assertGreater(abs(control_residual(control + .02, supply, 31423)),
                           LIMITS['code_adjusted_control_V'])

    def test_missing_report_replaces_previous_pass(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            output = root / 'checks.json'
            output.write_text('{"status": "pass"}\n')
            with self.assertRaises(FileNotFoundError):
                compare(root / 'cpp.json', root / 'va.json', output)
            self.assertEqual('fail', json.loads(output.read_text())['status'])

    def test_stale_source_replaces_previous_pass(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            source = root / 'circuit.hpp'
            source.write_text('changed circuit\n')
            paths = [root / (flow + '.json') for flow in ('cpp', 'veriloga')]
            for path, flow in zip(paths, ('cpp', 'veriloga')):
                path.write_text(json.dumps(dict(status='pass', flow=flow,
                    source_sha256={str(source): '0' * 64})))
            output = root / 'checks.json'
            output.write_text('{"status": "pass"}\n')
            with self.assertRaisesRegex(ValueError, 'stale closed-clock'):
                compare(*paths, output)
            self.assertEqual('fail', json.loads(output.read_text())['status'])

    def test_modified_waveform_is_rejected(self):
        with tempfile.TemporaryDirectory() as directory:
            path = Path(directory) / 'trace.csv.gz'
            original = (','.join(HEADER) + '\n' + '\n'.join(
                f'{i * 1e-12},0,0,0,1.2,0' for i in range(12)) + '\n').encode()
            checksum = hashlib.sha256(original).hexdigest()
            with gzip.open(path, 'wb') as stream:
                stream.write(original)
            self.assertEqual(12, len(read_trace(path, 'cpp', checksum)))
            with gzip.open(path, 'wb') as stream:
                stream.write(original.replace(b'1.2', b'1.1'))
            with self.assertRaisesRegex(ValueError, 'waveform changed'):
                read_trace(path, 'cpp', checksum)


if __name__ == '__main__':
    unittest.main()
