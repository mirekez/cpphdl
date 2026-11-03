#!/usr/bin/env python3
"""Guard device/port preservation and fail-before-mutation for bias routing."""
import argparse
import copy
from pathlib import Path
import sys
import unittest

import klayout.db as db

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))
from transmit_bias_layout import create_transmit_bias
from transmit_bias_reference_access import add_reference_access


def snapshot(layout):
    return {cell.name: dict(
        instances=sorted(str(inst) for inst in cell.each_inst()),
        shapes={(layout.get_info(layer).layer, layout.get_info(layer).datatype):
                sorted(str(shape) for shape in cell.each_shape(layer))
                for layer in layout.layer_indexes() if not cell.shapes(layer).is_empty()})
            for cell in layout.each_cell()}


class ReferenceAccess(unittest.TestCase):
    def setUp(self):
        self.layout, self.top, self.info = create_transmit_bias(PDK)

    def reject_without_mutation(self):
        before = snapshot(self.layout)
        metadata = copy.deepcopy(self.info)
        with self.assertRaises(ValueError):
            add_reference_access(self.layout, self.top, self.info)
        self.assertEqual(before, snapshot(self.layout))
        self.assertEqual(metadata, self.info)

    def test_devices_ports_and_existing_conductors_preserved(self):
        before = snapshot(self.layout)
        metadata = copy.deepcopy(self.info)
        allowed = {(layer, 0) for layer in (8, 19, 10, 29, 30, 49, 50, 66, 67, 125, 126, 133, 134)}
        regions = {key: db.Region(self.top.begin_shapes_rec(self.layout.layer(*key))) for key in allowed}
        add_reference_access(self.layout, self.top, self.info)
        after = snapshot(self.layout)
        self.assertEqual(set(before), set(after))
        for name in before:
            self.assertEqual(before[name]['instances'], after[name]['instances'])
            for key in set(before[name]['shapes']) | set(after[name]['shapes']):
                if name != self.top.name or key not in allowed:
                    self.assertEqual(before[name]['shapes'].get(key, []), after[name]['shapes'].get(key, []))
        for key, region in regions.items():
            final = db.Region(self.top.begin_shapes_rec(self.layout.layer(*key)))
            self.assertTrue((region - final).is_empty(), key)
        for key in ('components', 'terminals', 'schematic', 'escapes', 'placement'):
            self.assertEqual(metadata[key], self.info[key])
        self.assertNotEqual(before[self.top.name], after[self.top.name])

    def test_unsupported_finger_geometry_rejected(self):
        reference, = [part for part in self.info['components'] if part['name'] == 'BiasReference']
        reference['geometry']['fingers'] = 8
        self.reject_without_mutation()

    def test_relocated_port_rejected(self):
        self.info['terminals']['VSS']['layer'] = 134
        self.reject_without_mutation()

    def test_missing_escape_rejected(self):
        self.info['escapes'] = [entry for entry in self.info['escapes']
                               if not (entry['component'] == 'BiasReference' and entry['terminal'] == 'S')]
        self.reject_without_mutation()

    def test_reapplication_rejected_before_mutation(self):
        add_reference_access(self.layout, self.top, self.info)
        self.reject_without_mutation()


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--pdk', type=Path, required=True)
    args, remaining = parser.parse_known_args()
    PDK = args.pdk.resolve()
    unittest.main(argv=[sys.argv[0], *remaining])
