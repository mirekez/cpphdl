import unittest

from core_receive_decision import CODES, HEADER, PERIOD, WARMUP
from core_receive_decision_timing import return_timing


def trajectory():
    rows = []
    for index, _ in enumerate(CODES):
        above = (index % 2 == 0) != (index >= 8)
        for tick in range(40):
            row = [0.] * len(HEADER)
            row[0] = WARMUP + index * PERIOD + tick * .1e-9
            row[12] = 1.2
            row[10] = 1.2 if (above if tick >= 25 else not above) else 0
            rows.append(row)
    return rows


class ReturnTiming(unittest.TestCase):
    def test_returns_and_crossing_brackets(self):
        records = return_timing(trajectory())
        for record in records:
            self.assertAlmostEqual(record['remains_valid_from_s'] / 1e-9, 2.5)
            crossing, = record['core_half_rail_crossings']
            self.assertEqual(crossing['direction'], 'rise' if record['expected_high'] else 'fall')
            self.assertAlmostEqual(crossing['bracket_s'][0] / 1e-9, 2.4)

    def test_late_glitch_moves_stable_boundary(self):
        rows = trajectory()
        rows[33][10] = 0
        self.assertAlmostEqual(return_timing(rows)[0]['remains_valid_from_s'] / 1e-9, 3.4)
        rows[39][10] = 0
        with self.assertRaises(ValueError):
            return_timing(rows)

    def test_already_correct_is_not_a_new_edge(self):
        rows = trajectory()
        for row in rows[:40]:
            row[10] = 1.2
        record = return_timing(rows)[0]
        self.assertTrue(record['already_valid_at_start'])
        self.assertEqual(record['remains_valid_from_s'], 0)
        self.assertEqual(record['core_half_rail_crossings'], [])


if __name__ == '__main__':
    unittest.main()
