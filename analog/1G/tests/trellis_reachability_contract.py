from copy import deepcopy
import unittest
from trellis_reachability import check


def register(d, q):
    return dict(connections={'D': [d], 'Q': [q], 'CLK': [1]},
                port_directions={'D': 'input', 'Q': 'output', 'CLK': 'input'})


class Reachability(unittest.TestCase):
    def setUp(self):
        self.module = dict(netnames={'reachable': {'bits': [2]}},
            ports={'clk': {'bits': [1], 'direction': 'input'},
                   'start_in': {'bits': [3], 'direction': 'input'},
                   'metric_in': {'bits': [4], 'direction': 'input'}},
            cells={'reach': register(5, 2), 'cost': register(4, 6),
                   'combine': dict(connections={'A': [2], 'B': [3], 'Y': [5]},
                       port_directions={'A': 'input', 'B': 'input', 'Y': 'output'})})

    def test_state_and_control_only(self):
        self.assertEqual(check(self.module)['status'], 'pass')

    def test_shared_core_hierarchy(self):
        self.module['netnames']['core.reachable'] = self.module['netnames'].pop('reachable')
        self.assertEqual(check(self.module)['status'], 'pass')
        self.module['cells']['combine']['connections']['B'] = [4]
        with self.assertRaises(ValueError):
            check(self.module)

    def test_metric_and_path_cost_are_rejected(self):
        for bit in (4, 6):
            module = deepcopy(self.module)
            module['cells']['combine']['connections']['B'] = [bit]
            with self.subTest(bit=bit), self.assertRaises(ValueError):
                check(module)

    def test_cycle_is_rejected(self):
        self.module['cells']['combine']['connections']['B'] = [5]
        with self.assertRaisesRegex(ValueError, 'cycle'):
            check(self.module)

    def test_metric_cannot_drive_clock_reset_or_enable(self):
        for pin in ('CLK', 'RESET_B', 'ENABLE'):
            module = deepcopy(self.module)
            module['cells']['reach']['connections'][pin] = [4]
            module['cells']['reach']['port_directions'][pin] = 'input'
            with self.subTest(pin=pin), self.assertRaises(ValueError):
                check(module)

    def test_control_reset_is_accepted(self):
        self.module['cells']['reach']['connections']['RESET_B'] = [3]
        self.module['cells']['reach']['port_directions']['RESET_B'] = 'input'
        self.assertEqual(check(self.module)['status'], 'pass')


if __name__ == '__main__':
    unittest.main()
