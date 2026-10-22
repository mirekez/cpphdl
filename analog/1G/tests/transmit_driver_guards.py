#!/usr/bin/env python3
"""Inject electrical faults into a passing trace and require their rejection."""
import copy
import json
from pathlib import Path
import sys
import unittest

from transmit_driver import contract, controls, read_saved, verify


class DriverGuards(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.root = Path(sys.argv[1])
        report = json.loads((cls.root / 'checks.json').read_text())
        if report['status'] != 'pass':
            raise ValueError('checker guards require a passing physical-driver experiment')
        case = cls.root / 'nominal'
        cls.rows = read_saved(Path(report['cases']['nominal']['waveform']), 'cpp')
        cls.words = controls(case / 'controls.csv')
        cls.interface = json.loads((case / 'interface.json').read_text())
        verify(contract(cls.rows, cls.words, cls.interface))

    def test_missing_differential_output_is_rejected(self):
        rows = copy.deepcopy(self.rows)
        for row in rows:
            row[17] = row[18] = (row[17] + row[18]) / 2
            row[19] = 0
        result = contract(rows, self.words, self.interface)
        self.assertFalse(result['checks']['weighted_levels'])
        with self.assertRaises(ValueError):
            verify(result)

    def test_removed_history_bank_is_rejected(self):
        rows = copy.deepcopy(self.rows)
        ui, startup = (self.interface[key] for key in ('ui_s', 'startup_s'))
        for row in rows:
            index = int((row[0] - startup) / ui)
            if 0 <= index < len(self.words):
                word = self.words[index]
                difference = .375 * (2 - word['code']) if word['valid'] else 0
                center = (row[17] + row[18]) / 2
                row[17], row[18], row[19] = center + difference / 2, center - difference / 2, difference
        self.assertFalse(contract(rows, self.words, self.interface)['checks']['weighted_levels'])

    def test_idealized_supply_and_disabled_power_are_rejected(self):
        rows = copy.deepcopy(self.rows)
        for row in rows:
            row[22] = 0
        result = contract(rows, self.words, self.interface)
        self.assertFalse(result['checks']['active_current'])
        self.assertFalse(result['checks']['disabled_current'])

    def test_ignored_external_load_is_rejected(self):
        interface = dict(self.interface, load_ohm=0)
        result = contract(self.rows, self.words, interface)
        self.assertFalse(result['checks']['weighted_levels'])
        self.assertFalse(result['checks']['open_load_breaks_nominal_swing'])

    def test_hidden_bias_filter_is_rejected(self):
        interface = dict(self.interface, filtered=False)
        result = contract(self.rows, self.words, interface)
        self.assertFalse(result['checks']['shared_bias_disturbance_visible'])


if __name__ == '__main__':
    # Keep the result path for setUpClass while hiding it from unittest's CLI.
    unittest.main(argv=[sys.argv[0]])
