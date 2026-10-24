#!/usr/bin/env python3
import unittest
from transmit_state_contract import check_apertures


class ApertureTest(unittest.TestCase):
    def fixture(self):
        word = dict(code=2, previous=2, valid=1, main_p=3, main_n=12, history_p=3, history_n=12)
        row = [0.] * 42
        for bank, key in enumerate(('main_p', 'main_n', 'history_p', 'history_n')):
            for bit in range(4):
                row[1 + 4 * bank + bit] = 3.3 if word[key] & (1 << bit) else 0.
                row[26 + 4 * bank + bit] = 1.2 if word[key] & (1 << bit) else 0.
        row[17:23] = [1., 1., 0., 1.6, 3.3, .010]
        rows = [[time] + row[1:] for time in (0., .8, .8, 1.)]
        interface = dict(load_ohm=100, startup_s=0., ui_s=1., warm_words=0, filtered=True)
        return rows, [word], interface

    def test_both_states_pass(self):
        rows, words, interface = self.fixture()
        result = check_apertures(rows, words, interface)
        self.assertEqual(result['maximum_all_state_plateau_error_V'], 0.)

    def test_bad_second_state_is_not_hidden(self):
        for column, value in [(19, .021), (1, 2.9), (26, .9), (22, .02)]:
            with self.subTest(column=column):
                rows, words, interface = self.fixture()
                rows[2][column] = value
                if column == 19:
                    rows[2][17] += value
                with self.assertRaises(ValueError):
                    check_apertures(rows, words, interface)

    def test_neighboring_discontinuity_is_not_hidden(self):
        rows, words, interface = self.fixture()
        rows[1][0] = rows[2][0] = .7
        rows[2][1] = 2.0
        with self.assertRaisesRegex(ValueError, 'steering gate'):
            check_apertures(rows, words, interface)


if __name__ == '__main__':
    unittest.main()
