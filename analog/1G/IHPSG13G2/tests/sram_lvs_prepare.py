"""Check physical preservation and unchanged foundry rules in SRAM preparation."""
import argparse
from pathlib import Path
import sys
import tempfile
import unittest
from unittest.mock import patch

import klayout.db as db

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))
from prepare_sram_lvs import HERE, prepare
from prepare_sram_lvs import build_reference
from sram_reference import MACRO


class Preparation(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.directory = tempfile.TemporaryDirectory()
        cls.work = Path(cls.directory.name) / 'prepared'
        cls.report = prepare(PDK, cls.work)
        cls.original, cls.prepared = db.Layout(), db.Layout()
        cls.original.read(str(PDK / 'ihp-sg13g2/libs.ref/sg13g2_sram/gds' / (MACRO + '.gds')))
        cls.prepared.read(str(cls.work / 'macro.gds'))

    @classmethod
    def tearDownClass(cls):
        cls.directory.cleanup()

    def test_every_local_shape_and_instance_preserved(self):
        a, b = self.original, self.prepared
        names = self.report['name_correspondences']
        self.assertEqual(len(names), 20)
        self.assertEqual(a.cells(), b.cells())
        self.assertEqual(a.top_cell().bbox(), b.top_cell().bbox())

        def instances(cell, rename):
            return sorted((rename.get(i.cell.name, i.cell.name), str(i.cplx_trans),
                           str(i.a), str(i.b), i.na, i.nb) for i in cell.each_inst())

        for cell in a.each_cell():
            other = b.cell(names.get(cell.name, cell.name))
            self.assertIsNotNone(other)
            self.assertEqual(instances(cell, names), instances(other, {}))
            for info in set(a.layer_infos()) | set(b.layer_infos()):
                before = sorted(str(s) for s in cell.shapes(a.layer(info)).each() if not s.is_text())
                after = sorted(str(s) for s in other.shapes(b.layer(info)).each() if not s.is_text())
                self.assertEqual(before, after, (cell.name, str(info)))

    def test_top_labels_preserved(self):
        def labels(layout):
            return sorted((str(layout.get_info(i)), str(s.text)) for i in layout.layer_indexes()
                          for s in layout.top_cell().shapes(i).each() if s.is_text())
        self.assertEqual(labels(self.original), labels(self.prepared))
        self.assertEqual(self.report['internal_texts_removed'], 388)

    def test_foundry_device_rules_unchanged(self):
        original = PDK / 'ihp-sg13g2/libs.tech/klayout/tech/lvs'
        prepared = self.work / 'pdk/ihp-sg13g2/libs.tech/klayout/tech/lvs'
        for path in original.rglob('*'):
            if path.is_file() and path.name != 'sg13g2.lvs':
                self.assertEqual(path.read_bytes(), (prepared / path.relative_to(original)).read_bytes())
        text = (prepared / 'sg13g2.lvs').read_text()
        for name in ('sram_lvs_alignment.rb', 'sram_lvs_hints.rb'):
            block = (HERE / name).read_text()
            self.assertEqual(text.count(block), 1)
            text = text.replace(block, '')
        text = text.replace('  max_depth(2)\n  max_branch_complexity(20)\n', '')
        self.assertEqual(text, (original / 'sg13g2.lvs').read_text())

    def test_existing_output_is_not_overwritten(self):
        with self.assertRaises(FileExistsError):
            prepare(PDK, self.work)

    def test_reference_input_hash_cannot_be_overwritten(self):
        def stale_reference(*args):
            result = build_reference(*args)
            name = next(name for name in result['source_sha256'] if name.endswith('.gds'))
            result['source_sha256'][name] = '0' * 64
            return result

        with patch('prepare_sram_lvs.build_reference', side_effect=stale_reference):
            with self.assertRaisesRegex(ValueError, 'reference generation input changed'):
                prepare(PDK, Path(self.directory.name) / 'stale')


if __name__ == '__main__':
    parser = argparse.ArgumentParser()
    parser.add_argument('--pdk', type=Path, required=True)
    args, rest = parser.parse_known_args()
    PDK = args.pdk.resolve()
    unittest.main(argv=[sys.argv[0], *rest])
