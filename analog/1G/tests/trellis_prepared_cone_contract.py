from copy import deepcopy
import unittest
from trellis_prepared_cone import check


class PreparedCone(unittest.TestCase):
    def setUp(self):
        self.module = dict(netnames={'core.selectors[0]': {'bits': [900]}},
            ports={'clk': dict(direction='input', bits=[1]),
                   'metric_in': dict(direction='input', bits=[2]),
                   'difference_in': dict(direction='input', bits=list(range(100, 604)))},
            cells={'state': dict(connections={'D': [901], 'Q': [900], 'CLK': [1]},
                                port_directions={'D': 'input', 'Q': 'output', 'CLK': 'input'}),
                   'logic': dict(connections={'A': list(range(100, 604, 21)), 'Y': [901]},
                                 port_directions={'A': 'input', 'Y': 'output'})})

    def test_prepared_cone(self):
        self.assertEqual(check(self.module)['directed_differences'], 24)

    def test_raw_metric_is_rejected_even_on_clock(self):
        for cell, pin in (('logic', 'A'), ('state', 'CLK')):
            module = deepcopy(self.module)
            module['cells'][cell]['connections'][pin].append(2)
            with self.subTest(cell=cell), self.assertRaisesRegex(ValueError, 'raw branch metrics'):
                check(module)

    def test_missing_difference_is_rejected(self):
        self.module['cells']['logic']['connections']['A'].pop()
        with self.assertRaisesRegex(ValueError, 'all 24'):
            check(self.module)

    def test_cycle_and_undriven_are_rejected(self):
        for bit in (901, 999):
            module = deepcopy(self.module)
            module['cells']['logic']['connections']['A'].append(bit)
            with self.subTest(bit=bit), self.assertRaises(ValueError):
                check(module)


if __name__ == '__main__':
    unittest.main()
