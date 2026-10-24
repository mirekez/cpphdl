#!/usr/bin/env python3
"""The process binder must preserve branch terminals and reject unknown laws."""
from pathlib import Path
import sys
import unittest

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))
from core_driver_process import contributions, PROGRAMMABLE_BINDINGS


def model(line):
    return 'module m;\n    analog begin\n' + line + '\n    end\nendmodule\n'


class ProcessBinding(unittest.TestCase):
    def test_ground_and_channel_terminals(self):
        devices, roles = contributions(model(
            'I(n2, n0) <+ cpphdl_channel(V(n3, n0), V(n2, n0), 0.06, 0.5, 1.0);'))
        self.assertEqual(roles, {'logic_pull_down': 1})
        self.assertEqual((devices[0]['a'], devices[0]['gate'], devices[0]['b'], devices[0]['body']),
                         ('n2', 'n3', '0', '0'))
        self.assertEqual(devices[0]['width_um'], 40)

    def test_pmos_well_and_passive_polarity(self):
        devices, roles = contributions(model(
            'I(n2, n4) <+ cpphdl_channel(V(n3, n4), V(n2, n4), 0.003, 0.7, -1.0);\n'
            'I(n4, n0) <+ V(n4, n0) / 200.0;'))
        self.assertEqual(devices[0]['body'], 'n4')
        self.assertEqual(devices[0]['model'], 'sg13_hv_pmos')
        self.assertEqual(devices[1], dict(kind='R', a='n4', b='0', value=200.))

    def test_unknown_or_changed_contributions_fail(self):
        for line in (
            'I(n2, n0) <+ 1.0;',
            'I(n2, n0) <+ V(n3, n0) / 200.0;',
            'I(n2, n0) <+ V(n2, n0) / -1.0;',
            'I(n2, n0) <+ cpphdl_channel(V(n3, n4), V(n2, n0), 0.06, 0.5, 1.0);',
            'I(n2, n0) <+ cpphdl_channel(V(n3, n0), V(n2, n0), 0.07, 0.5, 1.0);'):
            with self.subTest(line=line), self.assertRaises(ValueError):
                contributions(model(line))

    def test_trim_pmos_requires_the_programmable_inventory(self):
        line = 'I(n2, n4) <+ cpphdl_channel(V(n3, n4), V(n2, n4), 0.02, 0.7, -1.0);'
        with self.assertRaises(ValueError):
            contributions(model(line))
        devices, roles = contributions(model(line), PROGRAMMABLE_BINDINGS)
        self.assertEqual(roles, {'trim_switch': 1})
        self.assertEqual(devices[0]['body'], 'n4')
        self.assertEqual(devices[0]['width_um'], 100)
        self.assertEqual(sum(entry[-1] for entry in PROGRAMMABLE_BINDINGS), 296)


if __name__ == '__main__':
    unittest.main()
