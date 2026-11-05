"""Reject fixed logical blocks and incomplete physical inventories."""
import copy
from pathlib import Path
import sys
import unittest

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))
from unified_synthesis import MACRO, check_cells, split_modules


class PlacementInventory(unittest.TestCase):
    def setUp(self):
        cells = {f'forwarding.memory_{i}': {'type': MACRO} for i in range(8)}
        cells.update({f'{group}.gate': {'type': 'sg13g2_inv_1'} for group in
                      ('port_a', 'port_b', 'forwarding', 'processing_clock', 'adc_clock')})
        self.design = {'modules': {'RepeaterDigital': {'cells': cells},
                       MACRO: {'attributes': {'blackbox': '1'}},
                       'sg13g2_inv_1': {'attributes': {'blackbox': '1'}}}}

    def test_leaf_inventory(self):
        result = check_cells(self.design)
        self.assertEqual(result['movable_standard_cells'], 5)
        self.assertEqual(result['fixed_logic_modules'], 0)

    def test_old_yosys_integer_attributes(self):
        self.design['modules'][MACRO]['attributes']['blackbox'] = 1
        self.design['modules']['sg13g2_inv_1']['attributes']['blackbox'] = 1
        self.assertEqual(check_cells(self.design)['total_instances'], 13)

    def test_reject_whole_phy_macro(self):
        self.design['modules']['RepeaterDigital']['cells']['port_a.gate']['type'] = 'RepeaterPort'
        self.design['modules']['RepeaterPort'] = {'attributes': {'blackbox': '1'}}
        with self.assertRaisesRegex(ValueError, 'logical module'):
            check_cells(self.design)

    def test_reject_missing_bank(self):
        del self.design['modules']['RepeaterDigital']['cells']['forwarding.memory_7']
        with self.assertRaisesRegex(ValueError, 'eight physical'):
            check_cells(self.design)

    def test_reject_missing_logic_group(self):
        for group in ('port_a', 'port_b', 'processing_clock', 'adc_clock'):
            with self.subTest(group=group):
                design = copy.deepcopy(self.design)
                del design['modules']['RepeaterDigital']['cells'][group + '.gate']
                with self.assertRaisesRegex(ValueError, 'lost top-level'):
                    check_cells(design)

    def test_reject_fake_leaf(self):
        self.design['modules']['sg13g2_inv_1']['cells'] = {'hidden_phy': {'type': 'RepeaterPort'}}
        with self.assertRaisesRegex(ValueError, 'non-leaf'):
            check_cells(self.design)

    def test_expand_logical_children(self):
        top = self.design['modules']['RepeaterDigital']['cells']
        for index in range(8):
            del top[f'forwarding.memory_{index}']
        self.design['modules']['Bank'] = {'cells': {f'm{index}': {'type': MACRO} for index in range(4)}}
        top['forwarding.a'] = {'type': 'Bank'}
        top['forwarding.b'] = {'type': 'Bank'}
        result = check_cells(self.design)
        self.assertEqual(len(result['sram_instances']), 8)
        self.assertEqual(result['total_instances'], 13)

    def test_partition_keeps_attributed_memory(self):
        source = ('module \\RepeaterDigital\n  wire input 1 \\a\nend\n'
                  'attribute \\top 1\nmodule \\SynchronousFrameFIFO\n'
                  '  wire input 1 \\a\nend\n')
        modules = split_modules(source)
        self.assertEqual(set(modules), {'RepeaterDigital', 'SynchronousFrameFIFO'})
        self.assertIn('attribute \\top 1', modules['SynchronousFrameFIFO'])
        with self.assertRaisesRegex(ValueError, 'partition'):
            split_modules(source + modules['RepeaterDigital'])


if __name__ == '__main__':
    unittest.main()
