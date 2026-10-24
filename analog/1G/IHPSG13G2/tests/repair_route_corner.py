#!/usr/bin/env python3
import sys
from pathlib import Path
import unittest
sys.path.insert(0, str(Path(__file__).resolve().parents[1]))
from repair_route_corner import repair

DEF = '''VERSION 5.8 ;
NETS 2 ;
 - a ( PIN a ) + ROUTED Metal3 ( 1000 1000 ) Via3_CoreAreaHV ( 2000 * 0 ) ( * 2000 ) ( 3000 * 0 ) ;
 - b ( PIN b ) + ROUTED Metal2 ( 500 500 ) ( 600 * ) ;
END NETS
'''


class CornerRepair(unittest.TestCase):
    def test_only_selected_interior_extension_changes(self):
        fixed, report = repair(DEF, 'a', 2000, 1000)
        self.assertEqual(fixed, DEF.replace('( 2000 * 0 )', '( 2000 * )'))
        self.assertEqual(report['coordinate_dbu'], [2000, 1000])
        self.assertIn('( 3000 * 0 )', fixed)

    def test_endpoints_missing_points_and_wrong_nets_rejected(self):
        for net, x, y in [('a', 1000, 1000), ('a', 3000, 2000), ('a', 999, 999), ('missing', 2000, 1000)]:
            with self.subTest(net=net, x=x, y=y), self.assertRaises(ValueError):
                repair(DEF, net, x, y)

    def test_straight_segment_and_via_boundary_rejected(self):
        for text in (DEF.replace('( * 2000 )', '( 2500 * )'),
                     DEF.replace('( 2000 * 0 ) ( * 2000 )', '( 2000 * 0 ) Via2_CoreAreaVH ( * 2000 )')):
            with self.assertRaises(ValueError):
                repair(text, 'a', 2000, 1000)

    def test_ambiguous_and_already_extended_points_rejected(self):
        for text in (DEF.replace('( 3000 * 0 )', '( 2000 1000 0 )'),
                     DEF.replace('( 2000 * 0 )', '( 2000 * )')):
            with self.assertRaises(ValueError):
                repair(text, 'a', 2000, 1000)


if __name__ == '__main__':
    unittest.main()
