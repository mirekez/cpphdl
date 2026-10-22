"""Reject missing slices and sample errors even when both solvers agree."""
import math
import csv
from pathlib import Path
import tempfile
import unittest
from programmable_receive_array import contract, verify, errors, read_cpp, read_spice, archive_trace, HEADER, WARMUP


def ideal_rows():
    symbols = (0, 1, -1, 2, -2, 0, 2, 1, -2, -1, 0, 1, 2, -1, -2, 0)
    held = [1.65] * 16
    rows = []
    next_sample = 0
    for tick in range(12801):
        ns = tick * .02
        symbol = min(31, int(max(0, ns - .1) // 8))
        differential = .09 * symbols[symbol % 16]
        gain_input = 1.65 + 1.25 * differential
        gain_output = 1.65 - .8 * (gain_input - 1.65)
        if next_sample < 64 and ns >= 4 * next_sample + 2.55:
            held[next_sample % 16] = gain_output
            next_sample += 1
        row = [WARMUP + ns * 1e-9, 1.65 + differential / 2, 1.65 - differential / 2,
               gain_input, gain_output, 3.3, 1.2, 1.65]
        rows.append(row + [value for level in held for value in (level, 0)])
    return rows


class ArrayContract(unittest.TestCase):
    def test_archived_cpp_and_spice_readers_preserve_every_sample(self):
        with tempfile.TemporaryDirectory() as temporary:
            rows = ideal_rows()
            for flow in ('cpp', 'spice'):
                path = Path(temporary) / ('trace.' + flow)
                with path.open('w') as output:
                    if flow == 'cpp':
                        writer = csv.writer(output)
                        writer.writerow(HEADER)
                        writer.writerows(rows)
                    else:
                        output.write('time ' + ' '.join(HEADER[1:]) + '\n')
                        for row in rows:
                            output.write(' '.join(map(str, row)) + '\n')
                archive, _ = archive_trace(path)
                restored = read_cpp(archive)[1] if flow == 'cpp' else read_spice(archive, HEADER)
                self.assertEqual(restored, rows)

    def test_all_slices_and_all_four_rounds(self):
        result = contract(ideal_rows(), 4)
        verify(result)
        self.assertEqual(result['samples_per_slice'], [4] * 16)

    def test_last_slice_must_acquire(self):
        rows = ideal_rows()
        for row in rows:
            row[38] = 2.3
        with self.assertRaisesRegex(ValueError, 'acquisition'):
            verify(contract(rows, 4))

    def test_signed_errors_cannot_cancel(self):
        rows = ideal_rows()
        for index, row in enumerate(rows):
            for slice in range(16):
                row[8 + 2 * slice] += .021 if index & 1 else -.021
        with self.assertRaisesRegex(ValueError, 'acquisition'):
            verify(contract(rows, 4))

    def test_hold_drift_has_its_own_limit(self):
        rows = ideal_rows()
        for index, row in enumerate(rows):
            row[8] += .006 if index & 1 else -.006
        with self.assertRaisesRegex(ValueError, 'hold_variation'):
            verify(contract(rows, 4))

    def test_truncated_final_round(self):
        with self.assertRaisesRegex(ValueError, 'window|aperture'):
            contract(ideal_rows()[:-200], 4)

    def test_each_track_and_both_supplies(self):
        for column, value, diagnosis in [(39, 3.3, 'track_closed'), (5, 3, 'analog_supply'),
                                         (6, 1, 'logic_supply')]:
            rows = ideal_rows()
            for row in rows:
                row[column] = value
            with self.subTest(column=column), self.assertRaisesRegex(ValueError, diagnosis):
                verify(contract(rows, 4))

    def test_nonfinite_sample(self):
        rows = ideal_rows()
        rows[0][38] = math.nan
        with self.assertRaises(ValueError):
            contract(rows, 4)

    def test_equal_time_states_are_all_checked(self):
        rows = ideal_rows()
        duplicate = list(rows[150])
        duplicate[8] += .021
        rows.insert(151, duplicate)
        with self.assertRaisesRegex(ValueError, 'acquisition'):
            verify(contract(rows, 4))
        self.assertGreater(errors(ideal_rows(), rows)[7], .020)

    def test_backward_time_is_rejected(self):
        rows = ideal_rows()
        rows[150][0] = rows[148][0]
        with self.assertRaisesRegex(ValueError, 'backward'):
            contract(rows, 4)


if __name__ == '__main__':
    unittest.main()
