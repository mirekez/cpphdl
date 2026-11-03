#!/usr/bin/env python3
"""Check failure-preserving publication and collision-safe GDS assembly."""
import argparse
import importlib.util
import json
from pathlib import Path
import sys
import tempfile
import unittest

HERE = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(HERE))
sys.path.insert(0, str(HERE.parents[1] / 'out/pdf-renderer'))
try:
    import klayout.db as db
    import PIL
except ImportError:
    print('KLayout Python and Pillow required')
    sys.exit(77)
spec = importlib.util.spec_from_file_location('publisher', HERE / 'package_development_layout.py')
publisher = importlib.util.module_from_spec(spec)
spec.loader.exec_module(publisher)


class Publication(unittest.TestCase):
    def make_gds(self, directory, name, width):
        layout = db.Layout()
        layout.dbu = .001
        child = layout.create_cell('SharedCellName')
        child.shapes(layout.layer(8, 0)).insert(db.Box(0, 0, width, 1000))
        top = layout.create_cell('SameTop')
        top.insert(db.CellInstArray(child.cell_index(), db.Trans(0, 0)))
        path = directory / (name + '.gds')
        layout.write(str(path))
        return path

    def test_failed_timing_published_and_duplicate_names_preserved(self):
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            entries = []
            for name, width in [('first', 1000), ('second', 2000)]:
                path = self.make_gds(root, name, width)
                receipt = root / (name + '.json')
                receipt.write_text(json.dumps(dict(status='fail', setup_ns=-0.7,
                    error='timing failed', artifact_sha256={str(path): publisher.digest(path)})))
                entries.append(dict(name=name, top='SameTop', source=str(path),
                    title=name, caption='Timing failed', status='failed', receipts=[str(receipt)]))
            selection = root / 'selection.json'
            selection.write_text(json.dumps(dict(blocks=entries)))
            output = root / 'output'
            publisher.run(argparse.Namespace(selection=selection, output=output, work=root / 'work',
                pdk=root / 'unused', tools=root / 'unused'))
            report = json.loads((output / 'development_provenance.json').read_text())
            self.assertFalse(report['full_chip_qualified'])
            self.assertFalse(report['electrically_integrated'])
            self.assertEqual(report['inter_block_connections_added'], 0)
            for block in report['blocks']:
                self.assertEqual(block['evidence'][0]['recorded']['setup_ns'], -.7)
                self.assertEqual(block['evidence'][0]['recorded']['status'], 'fail')
                self.assertTrue(block['evidence'][0]['source_hash_recorded'])
            layout, top = publisher.read_block(output / 'repeater_development.gds.gz', report['top'])
            self.assertEqual(len(list(top.each_inst())), 2)
            self.assertEqual(layout.cell('B00_SameTop').bbox().width(), 1000)
            self.assertEqual(layout.cell('B01_SameTop').bbox().width(), 2000)
            self.assertNotEqual(report['blocks'][0]['geometry_sha256'], report['blocks'][1]['geometry_sha256'])
            for name, data in report['artifacts'].items():
                self.assertEqual(publisher.digest(output / name), data['sha256'])

    def test_changed_input_rejected(self):
        with tempfile.TemporaryDirectory() as temporary:
            path = Path(temporary) / 'source'
            path.write_text('original')
            hashes = {str(path): publisher.digest(path)}
            path.write_text('replacement')
            with self.assertRaisesRegex(ValueError, 'input changed'):
                publisher.verify(hashes)

    def test_missing_physical_top_rejected(self):
        with tempfile.TemporaryDirectory() as temporary:
            path = self.make_gds(Path(temporary), 'source', 1000)
            with self.assertRaisesRegex(ValueError, 'physical top'):
                publisher.read_block(path, 'NotPresent')

    def test_geometry_change_detected(self):
        layout = db.Layout()
        top = layout.create_cell('Top')
        shapes = top.shapes(layout.layer(8, 0))
        shapes.insert(db.Box(0, 0, 100, 100))
        original = publisher.hierarchy_digest(top)
        top.name = 'Renamed'
        self.assertEqual(publisher.hierarchy_digest(top), original)
        shapes.insert(db.Box(200, 0, 300, 100))
        self.assertNotEqual(publisher.hierarchy_digest(top), original)


if __name__ == '__main__':
    unittest.main()
