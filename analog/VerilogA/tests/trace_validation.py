"""Preserve the comparison contract while bounding validation memory."""
from array import array
from pathlib import Path
import sys
import tracemalloc
import unittest

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))
from compare import checked_rows
from trace_validation import validate_rows


class TraceValidation(unittest.TestCase):
    def test_matches_existing_acceptance(self):
        for count in (0, 9, 10, 4096, 4097, 8200):
            original = [array('d', (float(i), 1.)) for i in range(count)]
            variants = [original]
            for at in (0, 4095, 4096, count - 1):
                if 0 <= at < count:
                    for bad in ([float(at), float('nan')], [float(at), float('inf')],
                                [float(at)], [float(at - 1), 1.]):
                        rows = original.copy()
                        rows[at] = bad
                        variants.append(rows)
            for rows in variants:
                outcomes = []
                for check in (checked_rows, validate_rows):
                    try:
                        check(rows, 2, 'regression')
                        outcomes.append(True)
                    except ValueError:
                        outcomes.append(False)
                self.assertEqual(outcomes[0], outcomes[1])

    def test_streaming_memory_is_bounded(self):
        # A second full float/list copy would consume tens of megabytes.
        # Generator input also verifies that validation never requires rewind.
        tracemalloc.start()
        try:
            count = validate_rows(([str(i), '1.25', '-2.5'] for i in range(100000)), 3, 'stream')
            _, peak = tracemalloc.get_traced_memory()
        finally:
            tracemalloc.stop()
        self.assertEqual(count, 100000)
        self.assertLess(peak, 100000)

    def test_preserves_owned_states(self):
        rows = [array('d', (i, i / 3)) for i in range(10)]
        before = [row.tobytes() for row in rows]
        self.assertEqual(validate_rows(rows, 2, 'owned'), 10)
        self.assertEqual(before, [row.tobytes() for row in rows])

    def test_bad_numeric_text_and_late_fault(self):
        for bad in ('bad', 'nan', 'inf'):
            rows = [[str(i), '1'] for i in range(100)]
            rows[-1][1] = bad
            with self.assertRaises(ValueError):
                validate_rows(iter(rows), 2, 'late fault')


if __name__ == '__main__':
    unittest.main()
