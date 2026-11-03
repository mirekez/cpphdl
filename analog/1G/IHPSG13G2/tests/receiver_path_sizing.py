"""Data-path sizing must exclude clock trees and prove replacement functions."""
from pathlib import Path
import sys
import unittest

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))
from receiver_path_sizing import data_trials, script

PATH = '''Startpoint: state
Endpoint: state
 0.1 ^ clock_buffer/X (sg13g2_buf_1)
 0.2 ^ state/Q (sg13g2_dfrbpq_1)
 0.3 ^ data/A (sg13g2_nand2_1)
 0.4 v data/Y (sg13g2_nand2_1)
 0.5 v state/D (sg13g2_dfrbpq_1)
 0.5 data arrival time
 0.1 ^ capture_buffer/X (sg13g2_buf_1)
'''
CELLS = dict(state=('sg13g2_dfrbpq_1', {}), data=('sg13g2_nand2_1', {}))
LIBRARY = {'sg13g2_dfrbpq_1': 'register', 'sg13g2_nand2_1': 'nand',
           'sg13g2_nand2_2': 'nand', 'sg13g2_nand2_4': 'wrong function',
           'sg13g2_nor2_2': 'nand', 'sg13g2_buf_1': 'buffer', 'sg13g2_buf_2': 'buffer'}


class ReceiverDataSizing(unittest.TestCase):
    def test_only_matching_data_function_once(self):
        self.assertEqual(data_trials(PATH + PATH, CELLS, LIBRARY), [['data', 'sg13g2_nand2_2']])

    def test_stale_cell_master_rejected(self):
        with self.assertRaisesRegex(ValueError, 'seed netlist'):
            data_trials(PATH.replace('data/A (sg13g2_nand2_1)', 'data/A (sg13g2_nand2_2)'), CELLS, LIBRARY)

    def test_missing_cell_rejected(self):
        with self.assertRaisesRegex(ValueError, 'seed netlist'):
            data_trials(PATH, {'state': CELLS['state']}, LIBRARY)

    def test_missing_launch_register_rejected(self):
        with self.assertRaisesRegex(ValueError, 'no proven'):
            data_trials(PATH.replace('/Q (', '/CLK ('), CELLS, LIBRARY)

    def test_missing_arrival_boundary_rejected(self):
        with self.assertRaisesRegex(ValueError, 'no proven'):
            data_trials(PATH.replace('data arrival time', 'incomplete'), CELLS, LIBRARY)

    def test_limit_and_clock_exclusion(self):
        library = dict(LIBRARY, sg13g2_nand2_8='nand')
        self.assertEqual(data_trials(PATH, CELLS, library, 1), [['data', 'sg13g2_nand2_2']])
        with self.assertRaises(ValueError):
            data_trials(PATH, CELLS, library, 201)

    def test_tcl_injection_rejected(self):
        with self.assertRaisesRegex(ValueError, 'unsafe'):
            script([['data;exit', 'sg13g2_nand2_2']])


if __name__ == '__main__':
    unittest.main()
