#!/usr/bin/env python3
"""Guard explicit SRAM reference changes and complete hierarchy traversal."""
from pathlib import Path
import sys
import unittest

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))
from sram_reference import reachable, rewrite_resistors, RESISTORS


class ReferenceGuards(unittest.TestCase):
    def test_indented_instances_and_repeated_children_survive(self):
        text = '''.SUBCKT top a b
  Xfirst a b / branch
Xsecond a b / branch
.ENDS
.SUBCKT branch a b
    Xleaf a b / leaf
.ENDS
.SUBCKT leaf a b
R0 a b lvsres w=2.6e-07 l=6e-07
.ENDS
.SUBCKT unused a b
.ENDS
'''
        _, counts = reachable(text, 'top')
        self.assertEqual(dict(counts), {'TOP': 1, 'BRANCH': 2, 'LEAF': 2})

    def test_missing_recursive_and_duplicate_hierarchy_rejected(self):
        for text in ('.SUBCKT top a\n  X0 a absent\n.ENDS\n',
                     '.SUBCKT top a\n  X0 a top\n.ENDS\n',
                     '.SUBCKT top a\n.ENDS\n.SUBCKT top b\n.ENDS\n'):
            with self.assertRaises(ValueError): reachable(text, 'top')

    def test_only_resistor_models_and_widths_change(self):
        transistor = 'MN0 Q QN VSS VSS sg13_lv_nmos w=600n l=130n ng=2'
        lines = [f'{name} {a} {b} lvsres w=2.6e-07 l=6e-07'
                 for name, (a, b, _) in RESISTORS.items()]
        text = '\n'.join(['.SUBCKT cell A B', transistor, *lines, '.ENDS'])
        result = rewrite_resistors(text, RESISTORS, '2.0e-07')
        self.assertIn(transistor, result)
        self.assertEqual(result.count('w=2.0e-07'), 6)
        self.assertEqual(result.count('res_metal2'), 4)
        self.assertEqual(result.count('res_metal3'), 2)
        for changed in (text.replace('w=2.6e-07', 'w=2.7e-07', 1),
                        text.replace('A_BLT_BOT A_BLT_TOP', 'A_BLT_BOT WRONG', 1),
                        text.replace(lines[0], ''), text.replace('.ENDS', lines[0] + '\n.ENDS')):
            with self.assertRaises(ValueError): rewrite_resistors(changed, RESISTORS, '2.0e-07')


if __name__ == '__main__':
    unittest.main()
