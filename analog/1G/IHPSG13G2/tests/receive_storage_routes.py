#!/usr/bin/env python3
"""Check storage access preserves the IC and matches independently verified GDS."""
import argparse
import copy
import json
from pathlib import Path
import sys
import unittest

import klayout.db as db

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))
from extract_analog import digest
from receive_common_storage_routes import add_receive_storage_routes

parser = argparse.ArgumentParser()
parser.add_argument('--layout', type=Path, required=True)
parser.add_argument('--candidate', type=Path)
args, remaining = parser.parse_known_args()


def checked_proof(path):
    proof = json.loads(path.read_text())
    if proof.get('status') != 'pass' or not proof.get('missing_device_rejected'):
        raise ValueError('passing receiver layout evidence required')
    for group in ('source_sha256', 'artifact_sha256'):
        for name, expected in proof[group].items():
            if digest(name) != expected:
                raise ValueError('changed receiver fixture: ' + name)
    return proof


def regions(layout, top):
    return {layout.get_info(i): db.Region(top.begin_shapes_rec(i))
            for i in layout.layer_indexes() if layout.get_info(i).datatype == 0}


class ReceiveStorageRoutes(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.proof = checked_proof(args.layout)

    def fixture(self):
        layout = db.Layout()
        layout.read(str(Path(self.proof['work']) / 'reference.gds'))
        return layout, layout.top_cell(), copy.deepcopy(self.proof['geometry'])

    def test_preserves_devices_storage_pins_and_original_conductors(self):
        layout, top, info = self.fixture()
        original = copy.deepcopy(info)
        before, bounds = regions(layout, top), top.bbox()
        add_receive_storage_routes(layout, top, info)
        after = regions(layout, top)
        for layer in set(before) | set(after):
            old, new = before.get(layer, db.Region()), after.get(layer, db.Region())
            self.assertEqual((old - new).count(), 0, str(layer))
            if layer.layer not in (67, 125, 126, 133, 134):
                self.assertEqual((old ^ new).count(), 0, str(layer))
        for key in ('components', 'schematic', 'terminals', 'escapes', 'export_inventory', 'kelvin_routes'):
            self.assertEqual(info[key], original[key], key)
        self.assertEqual(top.bbox(), bounds)

    def test_invalid_input_cannot_partially_connect_storage(self):
        for fault in ('unprepared', 'missing_bank', 'binding', 'count', 'dimensions', 'placement', 'pin'):
            with self.subTest(fault=fault):
                layout, top, info = self.fixture()
                bank = next(p for p in info['components'] if p['name'] == 'ReceiveCommonStorage17')
                if fault == 'unprepared':
                    del info['sink_upper_current']
                elif fault == 'missing_bank':
                    info['components'].remove(bank)
                elif fault == 'binding':
                    bank['connections']['PLUS'] = 'OUT'
                elif fault == 'count':
                    bank['geometry']['capacitors'] = 1
                elif fault == 'dimensions':
                    bank['geometry']['unit']['width_um'] = 50
                elif fault == 'placement':
                    bank['origin_um'][0] = float('inf')
                else:
                    info['terminals']['OUT']['layer'] = 67
                before, metadata = regions(layout, top), copy.deepcopy(info)
                with self.assertRaises(ValueError):
                    add_receive_storage_routes(layout, top, info)
                after = regions(layout, top)
                self.assertEqual(set(before), set(after))
                for layer in before:
                    self.assertEqual((before[layer] ^ after[layer]).count(), 0, str(layer))
                self.assertEqual(info, metadata)

    def test_duplicate_access_rejected(self):
        layout, top, info = self.fixture()
        add_receive_storage_routes(layout, top, info)
        with self.assertRaises(ValueError):
            add_receive_storage_routes(layout, top, info)

    @unittest.skipUnless(args.candidate, 'no independently checked candidate supplied')
    def test_matches_independently_checked_candidate(self):
        proof = checked_proof(args.candidate)
        expected = db.Layout()
        expected.read(str(Path(proof['work']) / 'reference.gds'))
        layout, top, info = self.fixture()
        add_receive_storage_routes(layout, top, info)
        actual, reference = regions(layout, top), regions(expected, expected.top_cell())
        self.assertEqual(set(actual), set(reference))
        for layer in actual:
            self.assertEqual((actual[layer] ^ reference[layer]).count(), 0, str(layer))
        self.assertEqual(info, proof['geometry'])


if __name__ == '__main__':
    unittest.main(argv=[sys.argv[0]] + remaining)
