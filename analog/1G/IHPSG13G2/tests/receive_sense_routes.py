#!/usr/bin/env python3
"""Guard receive-reference sense routing and preserve devices/current conductors."""
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


class ReceiveSenseRoutes(unittest.TestCase):
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
        return layout, top, info

    def test_only_declared_sense_branches_removed(self):
        layout, top, info = self.fixture()
        components, schematic = copy.deepcopy(info['components']), info['schematic']
        before = regions(layout, top)
        add_receive_sense_routes(layout, top, info)
        allowed = db.Region()
        for branch in info['kelvin_routes']['branches']:
            x, y = branch['sense_escape_um']
            _, py = branch['external_pin_um']
            allowed.insert(db.Box(*(round(v / layout.dbu)
                for v in (x - .2, py - .2, x + .2, y + .2))))
        with tempfile.TemporaryDirectory() as directory:
            path = Path(directory) / 'sense.gds'
            layout.write(str(path))
            restored = db.Layout()
            restored.read(str(path))
            after = regions(restored, restored.top_cell())
            for layer, original in before.items():
                removed = original - after.get(layer, db.Region())
                if layer.layer == 50:
                    removed -= allowed
                # Text-only PCell layers can retain an empty recursive
                # iterator through GDS import. Count actual lost polygons.
                self.assertEqual(removed.count(), 0, str(layer))
        self.assertEqual(info['components'], components)
        self.assertEqual(info['schematic'], schematic)
        self.assertEqual({b['net'] for b in info['kelvin_routes']['branches']}, {'VDD', 'VSS', 'OUT'})

    def test_wrong_feedback_binding_rejected(self):
        layout, top, info = self.fixture()
        device = next(p for p in info['components'] if p['name'] == 'ReceiveCommonMOS1')
        device['connections']['G'] = 'TARGET'
        with self.assertRaisesRegex(ValueError, 'sense topology'):
            add_receive_sense_routes(layout, top, info)

    def test_duplicate_escape_rejected(self):
        layout, top, info = self.fixture()
        info['escapes'].append(copy.deepcopy(next(e for e in info['escapes']
            if e['component'] == 'ReceiveCommonMOS1' and e['terminal'] == 'G')))
        with self.assertRaisesRegex(ValueError, 'sense escape binding'):
            add_receive_sense_routes(layout, top, info)

    def test_wrong_supply_pin_layer_rejected(self):
        layout, top, info = self.fixture()
        info['terminals']['VSS']['layer'] = 67
        with self.assertRaisesRegex(ValueError, 'source pin layer'):
            add_receive_sense_routes(layout, top, info)

    def test_missing_branch_rejected_without_partial_rewire(self):
        layout, top, info = self.fixture()
        escape = next(e for e in info['escapes']
            if e['component'] == 'ReceiveCommonMOS1' and e['terminal'] == 'G')
        x, y = escape['location_um']
        py = info['terminals']['OUT']['y_um']
        bounds = db.Box(*(round(v / layout.dbu) for v in (x - .2, py - .2, x + .2, y + .2)))
        victim, = [s for s in top.each_shape(layout.layer(50, 0)) if s.bbox() == bounds]
        victim.delete()
        before = regions(layout, top)
        with self.assertRaisesRegex(ValueError, 'loaded sense branch'):
            add_receive_sense_routes(layout, top, info)
        after = regions(layout, top)
        for layer, original in before.items():
            self.assertTrue((original ^ after[layer]).is_empty(), str(layer))
        self.assertNotIn('kelvin_routes', info)

    def test_unprepared_repeated_and_changed_width_rejected(self):
        for fault in ('unprepared', 'repeated', 'width'):
            with self.subTest(fault=fault):
                layout, top, info = self.fixture()
                if fault == 'unprepared':
                    del info['receive_drain_contacts']
                elif fault == 'repeated':
                    add_receive_sense_routes(layout, top, info)
                else:
                    info['via_columns'] = 4
                with self.assertRaisesRegex(ValueError, 'overlay once'):
                    add_receive_sense_routes(layout, top, info)

    @unittest.skipUnless(args.candidate, 'no independently checked candidate supplied')
    def test_matches_independently_checked_candidate_geometry(self):
        proof = checked_proof(args.candidate)
        expected = db.Layout()
        expected.read(str(Path(proof['work']) / 'reference.gds'))
        layout, top, info = self.fixture()
        add_receive_sense_routes(layout, top, info)
        actual, reference = regions(layout, top), regions(expected, expected.top_cell())
        self.assertEqual(set(actual), set(reference))
        for layer in actual:
            self.assertTrue((actual[layer] ^ reference[layer]).is_empty(), str(layer))
        self.assertEqual(info, proof['geometry'])


if __name__ == '__main__':
    unittest.main(argv=[sys.argv[0]] + remaining)
