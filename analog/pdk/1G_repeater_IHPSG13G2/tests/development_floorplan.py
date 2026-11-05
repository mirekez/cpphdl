#!/usr/bin/env python3
"""Check candidate preservation, routing clearances and real pad intrusion."""
from copy import deepcopy
import json
from pathlib import Path
import sys
import tempfile
import unittest

HERE = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(HERE))
sys.path.insert(0, str(HERE.parents[1] / 'out/pdf-renderer'))
import klayout.db as db
from floorplan_development_layout import compose, plan_blocks, update_candidates
from package_development_layout import digest, hierarchy_digest, read_block


class Floorplan(unittest.TestCase):
    def setUp(self):
        self.source = db.Layout()
        self.source.dbu = .001
        self.records = []
        for name in ('first', 'second'):
            child = self.source.create_cell(name + '_child')
            child.shapes(self.source.layer(8, 0)).insert(db.Box(-2000, -3000, 98000, 97000))
            cell = self.source.create_cell(name)
            cell.insert(db.CellInstArray(child.cell_index(), db.Trans()))
            self.records.append(dict(name=name, assembly_cell=name, width_um=100, height_um=100,
                                     geometry_sha256=hierarchy_digest(cell)))
        self.records.append(dict(name='padframe_current', assembly_cell='shared_padframe'))
        self.pad = db.Layout()
        self.pad.dbu = .001
        top = self.pad.create_cell('Padframe')
        ring = db.Region(db.Box(0, 0, 1040000, 1040000)) - db.Region(db.Box(200000, 200000, 840000, 840000))
        top.shapes(self.pad.layer(8, 0)).insert(ring)
        self.pad_report = dict(top='Padframe', core_bbox_um=[200, 200, 840, 840])
        self.plan = dict(core_opening_um=640, core_margin_um=100, channel_um=100,
                         positions_um=dict(first=[100, 100], second=[300, 100]))

    def test_round_trip_preserves_geometry_and_placement(self):
        layout, top, placed, pad = compose(self.source, self.records, self.pad, self.pad_report, self.plan)
        with tempfile.TemporaryDirectory() as directory:
            path = Path(directory) / 'floorplan.gds.gz'
            layout.write(str(path))
            restored, restored_top = read_block(path, top.name)
            self.assertEqual(len(restored.top_cells()), 1)
            self.assertEqual(len(list(restored_top.each_inst())), 3)
            for block in placed:
                instance = next(i for i in restored_top.each_inst() if i.cell.name == block['assembly_cell'])
                self.assertEqual(hierarchy_digest(instance.cell), block['geometry_sha256'])
                self.assertEqual(instance.dbbox(), db.DBox(*block['bounds_um']))

    def test_insufficient_routing_channel_rejected(self):
        self.plan['positions_um']['second'][0] = 299.999
        with self.assertRaisesRegex(ValueError, 'channel collision'):
            plan_blocks(self.records, self.plan, [200, 200])

    def test_missing_module_rejected(self):
        del self.plan['positions_um']['second']
        with self.assertRaisesRegex(ValueError, 'inventory'):
            plan_blocks(self.records, self.plan, [200, 200])

    def test_grown_module_rejected(self):
        self.records[0]['width_um'] = 600
        with self.assertRaisesRegex(ValueError, 'core margin'):
            plan_blocks(self.records, self.plan, [200, 200])

    def test_mutated_geometry_rejected(self):
        self.source.cell('first_child').shapes(self.source.layer(10, 0)).insert(db.Box(0, 0, 50, 50))
        with self.assertRaisesRegex(ValueError, 'geometry changed'):
            compose(self.source, self.records, self.pad, self.pad_report, self.plan)

    def test_pad_metal_intrusion_rejected(self):
        self.pad.cell('Padframe').shapes(self.pad.layer(30, 0)).insert(db.Box(310000, 310000, 320000, 320000))
        with self.assertRaisesRegex(ValueError, 'padframe intrudes'):
            compose(self.source, self.records, self.pad, self.pad_report, self.plan)

    def test_boundary_marker_is_not_pad_metal(self):
        self.pad.cell('Padframe').shapes(self.pad.layer(39, 4)).insert(db.Box(0, 0, 1040000, 1040000))
        layout, top, placed, pad = compose(self.source, self.records, self.pad, self.pad_report, self.plan)
        self.assertEqual(len(placed), 2)

    def test_stale_dimensions_rejected(self):
        self.records[0]['width_um'] = 90
        with self.assertRaisesRegex(ValueError, 'dimensions differ'):
            compose(self.source, self.records, self.pad, self.pad_report, self.plan)

    def test_actual_published_inventory_fits_reviewed_plan(self):
        root = HERE.parents[1]
        report = json.loads((root / '1G/IHPSG13G2/results/development_provenance.json').read_text())
        plan = json.loads((HERE / 'development_floorplan.json').read_text())
        records = report['blocks']
        # Derive GDS dimensions from the actual replacement. For OpenDB, use
        # only a published measurement bound to this exact source database.
        for replacement in plan.get('replacements', []):
            entry = replacement['block']
            source = root / entry['source']
            if entry['format'] == 'gds':
                layout, top = read_block(source, entry['top'])
                width, height = top.dbbox().width(), top.dbbox().height()
                layout.destroy()
            else:
                measured = next(b for b in report['blocks'] if b['name'] == entry['name'])
                self.assertEqual(measured['source_sha256'], digest(source),
                                 'publish measured dimensions for the changed OpenDB candidate')
                width, height = measured['width_um'], measured['height_um']
            removed = set(replacement['replaces']) | {entry['name']}
            records = [b for b in records if b['name'] not in removed]
            records.append(dict(name=entry['name'], width_um=width, height_um=height))
        placed = plan_blocks(records, plan, [320, 320])
        self.assertEqual(len(placed), len(plan['positions_um']))

    def test_partial_replacement_rejected(self):
        with tempfile.TemporaryDirectory() as directory:
            with self.assertRaisesRegex(ValueError, 'incomplete replacement'):
                update_candidates(self.source, self.records,
                    [dict(replaces=['first', 'missing'], block=dict(name='combined'))],
                    Path(directory), Path(directory), Path(directory))

    def test_replacement_round_trip_and_repeat(self):
        with tempfile.TemporaryDirectory() as directory:
            work = Path(directory)
            replacement = db.Layout()
            replacement.dbu = .001
            cell = replacement.create_cell('Combined')
            cell.shapes(replacement.layer(8, 0)).insert(db.Box(0, 0, 200000, 100000))
            gds = work / 'combined.gds'
            replacement.write(str(gds))
            entries = [dict(replaces=['first', 'second'], block=dict(
                name='combined', source=str(gds), top='Combined', format='gds'))]
            records, hashes, changes = update_candidates(
                self.source, self.records, entries, work, work, work)
            self.assertEqual({r['name'] for r in records}, {'combined', 'padframe_current'})
            self.assertEqual(records[0]['width_um'], 200)
            self.assertEqual(records[0]['geometry_sha256'], hierarchy_digest(cell))
            repeated, _, _ = update_candidates(self.source, records, entries, work, work, work)
            self.assertEqual(repeated[0]['geometry_sha256'], records[0]['geometry_sha256'])


if __name__ == '__main__':
    unittest.main()
