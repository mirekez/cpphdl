#!/usr/bin/env python3
"""Check packed waveform storage without changing samples or validation."""
from pathlib import Path
import sys
import unittest

sys.path.insert(0, str(Path(__file__).resolve().parents[3] / 'VerilogA'))
from compact_trace import checked_rows, project


class CompactTrace(unittest.TestCase):
    def setUp(self):
        self.reference = [[i * .1, i / 3, -i / 7] for i in range(100)]
        self.rows = checked_rows(self.reference, 3, 'fixture')

    def test_every_sample_is_preserved(self):
        self.assertEqual([list(row) for row in self.rows], self.reference)
        self.assertEqual(self.rows.data.nbytes, 100 * 3 * 8)
        self.assertTrue(self.rows.data.readonly)

    def test_views_share_storage_and_keep_python_indexing(self):
        for selection in (slice(1, None), slice(None, None, -1), slice(8, 60, 3)):
            view = self.rows[selection]
            self.assertIs(view.data, self.rows.data)
            self.assertEqual([list(row) for row in view], self.reference[selection])
        self.assertEqual(list(self.rows[-1]), self.reference[-1])
        with self.assertRaises(IndexError):
            self.rows[100]

    def test_projection_preserves_all_rows(self):
        for rows in (self.reference, self.rows):
            view = project(rows, 2)
            self.assertEqual([list(row) for row in view], [row[:2] for row in self.reference])
            self.assertEqual([list(row) for row in view[2:8:2]], [row[:2] for row in self.reference[2:8:2]])

    def test_invalid_traces_still_fail(self):
        for bad in (self.reference[:9], [[0, 1]] * 10,
                    [[float('nan'), 1, 2]] * 10,
                    [self.reference[0], self.reference[0], *self.reference[2:]]):
            with self.assertRaises(ValueError):
                checked_rows(bad, 3, 'negative')


if __name__ == '__main__':
    unittest.main()
