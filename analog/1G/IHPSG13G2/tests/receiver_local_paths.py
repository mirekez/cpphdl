"""Select buffers from data paths without accidentally editing clock/hold trees."""
from pathlib import Path
import sys
import unittest

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))
from receiver_local_route import path_buffers

PATH = '''Startpoint: state
Endpoint: result
 ^ wire10/X (sg13g2_buf_2)
 ^ state/Q (sg13g2_dfrbpq_1)
 ^ place20/A (sg13g2_buf_2)
 ^ place20/X (sg13g2_buf_2)
 ^ wire30/X (sg13g2_buf_4)
 ^ hold40/X (sg13g2_dlygate4sd1_1)
 ^ result/D (sg13g2_dfrbpq_1)
 data arrival time
 ^ wire50/X (sg13g2_buf_2)
'''


class LocalPaths(unittest.TestCase):
    def test_clocks_and_hold_cells_excluded(self):
        self.assertEqual(path_buffers(PATH), ['place20', 'wire30'])

    def test_overlapping_path_reports_do_not_duplicate_cells(self):
        self.assertEqual(path_buffers(PATH + PATH), ['place20', 'wire30'])

    def test_incomplete_path_rejected(self):
        for text in (PATH.replace('/Q (', '/CLK ('), PATH.replace('data arrival time', 'missing')):
            with self.assertRaises(ValueError):
                path_buffers(text)

    def test_explicit_limit(self):
        paths = ''.join(PATH.replace('place20', 'place' + str(1000 + i)) for i in range(210))
        names = path_buffers(paths)
        self.assertEqual(len(names), 200)
        self.assertEqual(len(set(names)), 200)


if __name__ == '__main__':
    unittest.main()
