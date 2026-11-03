"""Reject incomplete or electrically failing global candidates before routing."""
import copy
from pathlib import Path
import sys
import unittest

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))
from receiver_route_probe import screen


class ProbeGuards(unittest.TestCase):
    def test_screen(self):
        good = dict(status='diagnostic_complete', rounds=[dict(name='after',
            connectivity=dict(status='pass', top='PCSReceiver'), electrical_pass=True,
            setup_ns=.2, hold_ns=.12)])
        screen(good)
        for key, value in [('electrical_pass', False), ('setup_ns', -.01), ('hold_ns', .09),
                           ('setup_ns', float('nan')), ('hold_ns', float('inf')), ('name', 'before')]:
            with self.subTest(key=key, value=value):
                broken = copy.deepcopy(good)
                broken['rounds'][-1][key] = value
                with self.assertRaises(ValueError):
                    screen(broken)
        for broken in ({}, dict(status='running', rounds=good['rounds']),
                       dict(status='diagnostic_complete', rounds=[])):
            with self.assertRaises(ValueError):
                screen(broken)
        for key, value in [('status', 'fail'), ('top', 'Other')]:
            broken = copy.deepcopy(good)
            broken['rounds'][-1]['connectivity'][key] = value
            with self.assertRaises(ValueError):
                screen(broken)


if __name__ == '__main__':
    unittest.main()
