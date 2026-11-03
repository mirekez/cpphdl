#!/usr/bin/env python3
"""Reject receive-routing topology changes and preserve the qualified devices."""
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

parser = argparse.ArgumentParser()
parser.add_argument('--layout', type=Path, required=True)
args, remaining = parser.parse_known_args()


class ReceiveRoutes(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.proof = json.loads(args.layout.read_text())
        if (cls.proof.get('status') != 'pass' or
                cls.proof.get('missing_device_rejected') is not True):
            raise ValueError('passing original receive layout required')
        for group in ('source_sha256', 'artifact_sha256'):
            for path, value in cls.proof[group].items():
                if digest(path) != value:
                    raise ValueError('stale receive geometry fixture: ' + path)
        cls.gds = Path(cls.proof['work']) / 'reference.gds'

    def fixture(self):
        layout = db.Layout()
        layout.read(str(self.gds))
        return layout, layout.top_cell(), copy.deepcopy(self.proof['geometry'])

    def test_devices_and_existing_conductors_survive_roundtrip(self):
        layout, top, info = self.fixture()
        original_components = copy.deepcopy(info['components'])
        original_schematic = info['schematic']
        layers = [layout.get_info(index) for index in layout.layer_indexes()
                  if layout.get_info(index).datatype == 0]
        before = {layer: db.Region(top.begin_shapes_rec(layout.layer(layer))) for layer in layers}
        add_receive_current_routes(layout, top, info)
        add_receive_drain_contacts(layout, top, info)
        self.assertEqual(info['components'], original_components)
        self.assertEqual(info['schematic'], original_schematic)
        with tempfile.TemporaryDirectory() as directory:
            path = Path(directory) / 'routes.gds'
            layout.write(str(path))
            restored = db.Layout()
            restored.read(str(path))
            cell = restored.top_cell()
            for layer, old in before.items():
                # Text-only marker layers can leave a nonempty recursive
                # iterator with zero polygons. Count actual missing polygons.
                missing = old - db.Region(cell.begin_shapes_rec(restored.layer(layer)))
                self.assertEqual(missing.count(), 0, 'original geometry removed on ' + str(layer))
            self.assertTrue(cell.shapes(restored.layer(67, 2)).is_empty())
            for net, pin in info['terminals'].items():
                point = db.Point(round(pin['x_um'] / restored.dbu), round(pin['y_um'] / restored.dbu))
                markers = [s.bbox() for s in cell.shapes(restored.layer(pin['layer'], 2)).each()
                           if s.bbox().contains(point)]
                self.assertEqual(len(markers), 1)
                conductor = db.Region(cell.begin_shapes_rec(restored.layer(pin['layer'], 0)))
                self.assertTrue((db.Region(markers[0]) - conductor).is_empty(), net)

    def test_wrong_sink_gate_rejected(self):
        layout, top, info = self.fixture()
        sink = next(p for p in info['components'] if p['name'] == 'ReceiveCommonMOS10')
        sink['connections']['G'] = 'VDD'
        with self.assertRaisesRegex(ValueError, 'sink binding'):
            add_receive_current_routes(layout, top, info)

    def test_missing_pin_rejected(self):
        layout, top, info = self.fixture()
        top.shapes(layout.layer(67, 2)).clear()
        with self.assertRaisesRegex(ValueError, 'ambiguous receive-reference pin'):
            add_receive_current_routes(layout, top, info)

    def test_changed_transistor_dimensions_rejected(self):
        layout, top, info = self.fixture()
        add_receive_current_routes(layout, top, info)
        sink = next(p for p in info['components'] if p['name'] == 'ReceiveCommonMOS10')
        sink['geometry']['length_um'] = .5
        with self.assertRaisesRegex(ValueError, 'contact geometry'):
            add_receive_drain_contacts(layout, top, info)

    def test_missing_diffusion_stripe_rejected(self):
        layout, top, info = self.fixture()
        add_receive_current_routes(layout, top, info)
        instance, = list(layout.cell('ReceiveCommonMOS10').each_inst())
        stripe = next(s for s in instance.cell.each_shape(layout.layer(8, 0))
                      if s.is_box() and s.bbox().height() == round(5 / layout.dbu))
        stripe.delete()
        with self.assertRaisesRegex(ValueError, 'diffusion inventory'):
            add_receive_drain_contacts(layout, top, info)

    def test_repeated_or_unprepared_overlay_rejected(self):
        layout, top, info = self.fixture()
        with self.assertRaisesRegex(ValueError, 'current-route overlay once'):
            add_receive_drain_contacts(layout, top, info)
        add_receive_current_routes(layout, top, info)
        with self.assertRaisesRegex(ValueError, 'current topology'):
            add_receive_current_routes(layout, top, info)
        add_receive_drain_contacts(layout, top, info)
        with self.assertRaisesRegex(ValueError, 'current-route overlay once'):
            add_receive_drain_contacts(layout, top, info)


if __name__ == '__main__':
    unittest.main(argv=[sys.argv[0]] + remaining)
