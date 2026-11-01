"""Guard exact validation semantics and bounded auxiliary memory."""
from array import array
from pathlib import Path
import sys
import tracemalloc
import unittest

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))
from compare import checked_rows
from validate_trace import validate_rows


class ValidationTests(unittest.TestCase):
    def test_valid_rows_are_unchanged(self):
        rows = [array('d', (i * .125, i * -.25)) for i in range(12)]
        before = [row.tobytes() for row in rows]
        self.assertEqual(validate_rows(rows, 2, 'trace'), len(checked_rows(rows, 2, 'trace')))
        self.assertEqual([row.tobytes() for row in rows], before)

    def test_rejections_match_original_validator(self):
        original = [[float(i), 1.] for i in range(12)]
        variants = [[], original[:9]]
        for replacement in ([3.], [3., 1., 2.], [3., float('nan')],
                            [3., float('inf')], [2., 1.], [-1., 1.], [3., 'invalid']):
            rows = [row[:] for row in original]
            rows[3] = replacement
            variants.append(rows)
        for rows in variants:
            with self.subTest(rows=rows):
                with self.assertRaises(ValueError) as previous:
                    checked_rows(rows, 2, 'trace')
                with self.assertRaises(ValueError) as current:
                    validate_rows(rows, 2, 'trace')
                self.assertEqual(str(current.exception), str(previous.exception))

    def test_single_pass_iterables(self):
        def rows():
            for i in range(12):
                yield [str(i), str(i * .25)]
        self.assertEqual(validate_rows(rows(), 2, 'trace'), len(checked_rows(rows(), 2, 'trace')))

    def test_auxiliary_memory_does_not_retain_trajectory(self):
        def rows():
            for i in range(10000):
                yield array('d', [i, *range(39)])
        tracemalloc.start()
        try:
            self.assertEqual(validate_rows(rows(), 40, 'trace'), 10000)
            _, peak = tracemalloc.get_traced_memory()
        finally:
            tracemalloc.stop()
        self.assertLess(peak, 1024 * 1024)


if __name__ == '__main__':
    unittest.main()
