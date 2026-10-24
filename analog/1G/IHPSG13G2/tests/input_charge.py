#!/usr/bin/env python3
"""Charge integration must include reverse current and reject broken evidence."""
import importlib.util
from pathlib import Path
import tempfile
import unittest

spec = importlib.util.spec_from_file_location('translator_charge', Path(__file__).resolve().parents[1] / 'input_charge.py')
charge = importlib.util.module_from_spec(spec)
spec.loader.exec_module(charge)


class InputCharge(unittest.TestCase):
    def waveform(self, capacitance):
        # Four full linear ramps; the middle two have complete integration windows.
        rows = []
        for step in range(10001):
            time = step * 1e-12
            command, current = 0.0, 0.0
            for index, begin in enumerate((2e-9, 4e-9, 6e-9, 8e-9)):
                direction = 1 if index % 2 == 0 else -1
                if time >= begin:
                    command += direction * min(1, (time - begin) / .1e-9)
                    if time < begin + .1e-9:
                        current += direction * capacitance / .1e-9
            rows.append((time, command, command - 50 * current))
        return rows

    def test_known_capacitance_and_overload_are_distinguished(self):
        for capacitance in (100e-15, 200e-15):
            result = charge.edge_charge(self.waveform(capacitance), 1, startup=0)
            self.assertEqual(result['measured_edges'], 2)
            self.assertAlmostEqual(result['absolute_effective_capacitance_F'] / capacitance, 1, delta=.011)
            self.assertEqual(result['absolute_effective_capacitance_F'] <= 160e-15, capacitance < 160e-15)

    def test_reverse_charge_is_counted_and_sparse_nonfinite_traces_rejected(self):
        rows = self.waveform(100e-15)
        reference = charge.edge_charge(rows, 1, startup=0)
        # Equal and opposite post-edge charge leaves net charge unchanged.
        altered = [(t, command, achieved - (0.001 if 6.4e-9 <= t < 6.45e-9 else
                                           -0.001 if 6.45e-9 <= t < 6.5e-9 else 0))
                   for t, command, achieved in rows]
        result = charge.edge_charge(altered, 1, startup=0)
        self.assertGreater(result['absolute_effective_capacitance_F'], reference['absolute_effective_capacitance_F'])
        for damaged in (rows[::20], rows[:3000], [*rows[:50], (float('nan'), 0, 0)]):
            with self.assertRaises(ValueError):
                charge.edge_charge(damaged, 1, startup=0)

    def test_changed_or_missing_artifacts_rejected(self):
        with tempfile.TemporaryDirectory() as directory:
            path = Path(directory) / 'trace.dat.gz'
            path.write_bytes(b'original evidence')
            expected = {str(path): charge.digest(path)}
            charge.verify_files(expected)
            path.write_bytes(b'different evidence')
            with self.assertRaisesRegex(ValueError, 'changed'):
                charge.verify_files(expected)
            with self.assertRaisesRegex(ValueError, 'missing'):
                charge.verify_files({})


if __name__ == '__main__':
    unittest.main()
