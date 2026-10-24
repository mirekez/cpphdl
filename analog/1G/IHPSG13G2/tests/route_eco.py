"""An incremental repair must retain every unselected routing guide."""
from pathlib import Path
import sys
import unittest
sys.path.insert(0, str(Path(__file__).resolve().parents[1]))
from route_eco import merge_guides, read_guides

BASE = 'a\n(\n0 0 10 10 Metal2\n)\nb\n(\n10 0 20 10 Metal3\n)\n'
DELTA = 'b\n(\n10 0 30 10 Metal3\n)\nc\n(\n20 0 30 10 Metal2\n)\n'


class RouteRepair(unittest.TestCase):
    def test_preserves_other_nets_and_adds_new_buffer_net(self):
        result = read_guides(merge_guides(BASE, DELTA, ['b', 'c']))
        self.assertEqual(result['a'], read_guides(BASE)['a'])
        self.assertEqual(result['b'], read_guides(DELTA)['b'])
        self.assertEqual(result['c'], read_guides(DELTA)['c'])

    def test_complete_router_output_is_accepted_only_with_unchanged_other_nets(self):
        whole = BASE.split('b\n')[0] + DELTA
        self.assertEqual(merge_guides(BASE, whole, ['b', 'c']), merge_guides(BASE, DELTA, ['b', 'c']))
        with self.assertRaisesRegex(ValueError, 'unselected'):
            merge_guides(BASE, whole.replace('0 0 10 10 Metal2', '0 0 11 10 Metal2'), ['b', 'c'])

    def test_missing_selected_net_is_rejected(self):
        with self.assertRaisesRegex(ValueError, 'missing'):
            merge_guides(BASE, DELTA, ['b', 'c', 'd'])

    def test_malformed_or_duplicate_guides_are_rejected(self):
        for text in (BASE + BASE, 'a\n(\n)\n', 'a\n(\n0 0 0 10 Metal2\n)\n',
                     'a\n(\n0 0 10 10 Metal2\n', 'a\n0 0 10 10 Metal2\n'):
            with self.subTest(text=text), self.assertRaises(ValueError):
                read_guides(text)


if __name__ == '__main__':
    unittest.main()
