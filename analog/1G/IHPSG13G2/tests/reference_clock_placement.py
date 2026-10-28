#!/usr/bin/env python3
"""Reject linear-distance shortcuts across physical PLL row boundaries."""
from pathlib import Path
import sys
import unittest

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))
from reference_clock_placement import placement_order, routing_cost
from reference_clock_hierarchy import physical_groups


class Cell:
    def __init__(self, name):
        self.name = name

    def bbox(self):
        return self

    def width(self):
        return 90


class FoldedPlacement(unittest.TestCase):
    def test_external_connection_reaches_left_spine(self):
        self.assertEqual(routing_cost([0, 1], [100, 100], {'reset': [1]}, {},
            row_width_um=250, left_anchored_nets={'reset'}), 150)

    def test_same_row_uses_span(self):
        self.assertEqual(routing_cost([0, 1], [100, 100], {'q': [0, 1]}, {},
            row_width_um=250, row_pitch_um=300), 100)

    def test_row_cut_includes_both_buses_and_spine(self):
        self.assertEqual(routing_cost([0, 1, 2, 3], [100] * 4, {'q': [1, 2]}, {},
            row_width_um=250, row_pitch_um=300), 150 + 50 + 300)

    def test_preceding_oscillator_consumes_row_space(self):
        self.assertEqual(routing_cost([0, 1], [100] * 2, {'q': [0, 1]}, {},
            row_width_um=250, prefix_width_um=100, row_pitch_um=300), 150 + 50 + 300)

    def test_multiple_rows_share_one_spine(self):
        self.assertEqual(routing_cost([0, 1, 2], [100] * 3, {'q': [0, 1, 2]}, {'q': 2},
            row_width_um=100, row_pitch_um=300), 2 * (3 * 50 + 2 * 300))

    def test_optimizer_keeps_storage_feedback_in_one_row(self):
        parts = [(Cell(str(i)), {}, {'G': 'q' if i in (1, 2) else 'VDD'}, 'mos')
                 for i in range(4)]
        order, evidence = placement_order(parts, 1, power_nets={'VDD'}, net_weights={},
            row_width_um=250, row_pitch_um=300)
        self.assertEqual(sorted(order), list(range(4)))
        self.assertEqual(evidence['initial_weighted_span_um'], 500)
        self.assertEqual(evidence['placed_weighted_span_um'], 100)

    def test_impossible_dimensions_rejected(self):
        parts = [(Cell('a'), {}, {'G': 'q'}, 'mos')]
        for options in ({'row_width_um': 99}, {'row_width_um': float('nan')},
                        {'row_width_um': 250, 'prefix_width_um': 251}, {'row_pitch_um': -1}):
            with self.assertRaises(ValueError):
                placement_order(parts, 1, power_nets=set(), net_weights={}, **options)


class HierarchyBinding(unittest.TestCase):
    model = '''
        // device 0, IC latch, example.hpp:1
        I(cpphdl_n1, Vss) <+ cpphdl_channel(V(REF, Vss), V(cpphdl_n1, Vss), 1, 1, 1);
        // device 1, IC latch, example.hpp:2
        I(cpphdl_n1, Vss) <+ 1e-14 * ddt(V(cpphdl_n1, Vss));
        // device 2, IC peer, example.hpp:3
        I(cpphdl_n1, Vss) <+ 2e-14 * ddt(V(cpphdl_n1, Vss));
        // device 3, IC peer, example.hpp:4
        I(OUT, Vss) <+ V(OUT, Vss) / 1000;
    '''

    def test_capacitors_share_one_physical_branch(self):
        self.assertEqual(physical_groups(self.model, {'latch': [0, 1, 2]}),
            {'latch': ['ReferenceMOS0', 'ReferenceStorage0']})

    def test_shared_capacitance_is_not_claimed_by_either_group(self):
        self.assertEqual(physical_groups(self.model, {'latch': [0, 1], 'peer': [2, 3]}),
            {'latch': ['ReferenceMOS0'], 'peer': ['ReferenceResistance1']})

    def test_unowned_capacitance_remains_outside_group(self):
        self.assertEqual(physical_groups(self.model, {'latch': [0, 1]}),
            {'latch': ['ReferenceMOS0']})

    def test_invalid_device_ownership_rejected(self):
        for hierarchy in ({}, {'a': []}, {'a': [True]}, {'a': [-1]},
                          {'a': [0, 0]}, {'a': [0], 'b': [0]}, {'a': [9]}):
            with self.subTest(hierarchy=hierarchy), self.assertRaises(ValueError):
                physical_groups(self.model, hierarchy)

    def test_unknown_contribution_rejected(self):
        with self.assertRaises(ValueError):
            physical_groups(self.model.replace('V(OUT, Vss) / 1000', 'sin($abstime)'), {'a': [0]})


if __name__ == '__main__':
    unittest.main()
