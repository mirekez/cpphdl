#!/usr/bin/env python3
"""Guard distributed sink routing without changing devices or unrelated conductors."""
import argparse
import copy
import json
from pathlib import Path
import sys
import tempfile
import unittest

import klayout.db as db

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))
from extract_analog import digest
from receive_common_current_routes import add_receive_current_routes
from receive_common_drain_contacts import add_receive_drain_contacts
from receive_common_sense_routes import add_receive_sense_routes
from receive_common_upper_current_routes import add_receive_upper_current_routes

parser = argparse.ArgumentParser()
parser.add_argument('--layout', type=Path, required=True)
parser.add_argument('--candidate', type=Path)
args, remaining = parser.parse_known_args()


def checked_proof(path):
    proof = json.loads(path.read_text())
    if proof.get('status') != 'pass' or proof.get('missing_device_rejected') is not True:
        raise ValueError('passing local receive geometry required')
    for group in ('source_sha256', 'artifact_sha256'):
        for source, expected in proof[group].items():
            if digest(source) != expected:
                raise ValueError('stale receive geometry fixture: ' + source)
    return proof


def regions(layout, top):
    return {layout.get_info(index): db.Region(top.begin_shapes_rec(index))
            for index in layout.layer_indexes() if layout.get_info(index).datatype == 0}


class ReceiveUpperCurrentRoutes(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.proof = checked_proof(args.layout)
        cls.gds = Path(cls.proof['work']) / 'reference.gds'

    def fixture(self):
        layout = db.Layout()
        layout.read(str(self.gds))
        top = layout.top_cell()
        info = copy.deepcopy(self.proof['geometry'])
        add_receive_current_routes(layout, top, info)
        add_receive_drain_contacts(layout, top, info)
        add_receive_sense_routes(layout, top, info)
        return layout, top, info

    def test_preserves_devices_and_all_other_current_paths(self):
        layout, top, info = self.fixture()
        components, schematic = copy.deepcopy(info['components']), info['schematic']
        bounds = top.bbox()
        before = regions(layout, top)
        column, = [c for c in info['direct_supply_columns'] if c['component'] == 'ReceiveCommonMOS10']
        x, y = column['x_um'], column['source_y_um']
        allowed = db.Region(db.Box(*(round(v / layout.dbu)
            for v in (x - 1.67, y - .62, x + 1.67, y + .62))))
        add_receive_upper_current_routes(layout, top, info)
        with tempfile.TemporaryDirectory() as directory:
            path = Path(directory) / 'current.gds'
            layout.write(str(path))
            restored = db.Layout()
            restored.read(str(path))
            after = regions(restored, restored.top_cell())
            for layer, original in before.items():
                removed = original - after.get(layer, db.Region())
                if layer.layer == 49:
                    removed -= allowed
                self.assertEqual(removed.count(), 0, str(layer))
        self.assertEqual(info['components'], components)
        self.assertEqual(info['schematic'], schematic)
        self.assertEqual(top.bbox(), bounds)
        # Count actual cuts touching each connected Metal3 collector, not
        # metadata counts. These are necessary aggregate capacities only.
        sink = next(p for p in components if p['name'] == 'ReceiveCommonMOS10')
        metal = db.Region(top.begin_shapes_rec(layout.layer(30, 0))).merged()
        vias = db.Region(top.begin_shapes_rec(layout.layer(49, 0))).merged()
        for terminal, expected in (('D', 133), ('S', 129)):
            x, y = [a + b for a, b in zip(sink['origin_um'], sink['geometry']['terminals_um'][terminal])]
            point = db.Point(round(x / layout.dbu), round(y / layout.dbu))
            collector, = [p for p in metal.each() if p.inside(point)]
            self.assertEqual(vias.interacting(db.Region(collector)).count(), expected)

    def test_invalid_inputs_do_not_partially_rewire(self):
        for fault in ('unprepared', 'via_width', 'device', 'binding', 'pin', 'placement', 'missing_cut'):
            with self.subTest(fault=fault):
                layout, top, info = self.fixture()
                sink = next(p for p in info['components'] if p['name'] == 'ReceiveCommonMOS10')
                if fault == 'unprepared':
                    del info['kelvin_routes']
                elif fault == 'via_width':
                    info['via_columns'] = 4
                elif fault == 'device':
                    sink['geometry']['fingers'] = 28
                elif fault == 'binding':
                    sink['connections']['D'] = 'DRIVE'
                elif fault == 'pin':
                    info['terminals']['VSS']['layer'] = 126
                elif fault == 'placement':
                    native, = list(layout.cell(sink['name']).each_inst())
                    native.trans = db.Trans(1, False, 0, 0)
                else:
                    column, = [c for c in info['direct_supply_columns'] if c['component'] == sink['name']]
                    x, y = column['x_um'], column['source_y_um']
                    area = db.Box(*(round(v / layout.dbu)
                        for v in (x - 1.67, y - .62, x + 1.67, y + .62)))
                    victim = next(s for s in top.each_shape(layout.layer(49, 0))
                        if area.contains(s.bbox().p1) and area.contains(s.bbox().p2))
                    victim.delete()
                before = regions(layout, top)
                metadata = copy.deepcopy(info)
                with self.assertRaises(ValueError):
                    add_receive_upper_current_routes(layout, top, info)
                after = regions(layout, top)
                for layer, original in before.items():
                    self.assertEqual((original ^ after[layer]).count(), 0, str(layer))
                self.assertEqual(info, metadata)

    def test_repeated_overlay_rejected(self):
        layout, top, info = self.fixture()
        add_receive_upper_current_routes(layout, top, info)
        with self.assertRaisesRegex(ValueError, 'overlay once'):
            add_receive_upper_current_routes(layout, top, info)

    @unittest.skipUnless(args.candidate, 'no independently checked candidate supplied')
    def test_matches_independently_checked_geometry(self):
        proof = checked_proof(args.candidate)
        expected = db.Layout()
        expected.read(str(Path(proof['work']) / 'reference.gds'))
        layout, top, info = self.fixture()
        add_receive_upper_current_routes(layout, top, info)
        actual, reference = regions(layout, top), regions(expected, expected.top_cell())
        self.assertEqual(set(actual), set(reference))
        for layer in actual:
            self.assertEqual((actual[layer] ^ reference[layer]).count(), 0, str(layer))
        self.assertEqual(info, proof['geometry'])


if __name__ == '__main__':
    unittest.main(argv=[sys.argv[0]] + remaining)
