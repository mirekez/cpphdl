#!/usr/bin/env python3
"""Keep placement reusable after recipe edits without accepting changed RTL."""
import importlib.util
import json
from pathlib import Path
import sys
import tempfile
import unittest

SOURCE = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(SOURCE))
spec = importlib.util.spec_from_file_location('recipe_archive', SOURCE / 'unified_archive_recipe.py')
archive = importlib.util.module_from_spec(spec)
spec.loader.exec_module(archive)


class Archive(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory()
        self.addCleanup(self.temp.cleanup)
        root = Path(self.temp.name)
        self.source, self.work = root / 'source', root / 'work'
        self.source.mkdir()
        self.work.mkdir()
        previous = archive.__file__
        archive.__file__ = str(self.source / 'unified_archive_recipe.py')
        self.addCleanup(setattr, archive, '__file__', previous)
        self.recipe = self.source / 'unified_net_repair.tcl'
        self.rtl = self.source / 'design.v'
        self.odb = self.work / 'placed.odb'
        for path in (self.recipe, self.rtl, self.odb):
            path.write_text(path.name + ' original\n')
        self.proof = self.work / 'checks.json'
        self.proof.write_text(json.dumps(dict(status='placed_unrouted',
            source_sha256={str(p): archive.digest(p) for p in (self.recipe, self.rtl)},
            artifact_sha256={str(self.odb): archive.digest(self.odb)})))

    def test_recipe_can_change_but_rtl_and_geometry_stay_live(self):
        target = archive.archive(self.proof)
        report = json.loads(target.read_text())
        self.recipe.write_text('improved repair procedure\n')
        self.assertNotIn(str(self.recipe), report['source_sha256'])
        self.assertIn(str(self.rtl), report['source_sha256'])
        hashes = {**report['source_sha256'], **report['artifact_sha256']}
        self.assertTrue(all(archive.digest(p) == h for p, h in hashes.items()))
        self.rtl.write_text('changed circuit\n')
        self.assertNotEqual(archive.digest(self.rtl), hashes[str(self.rtl)])
        self.odb.write_text('changed placement\n')
        self.assertNotEqual(archive.digest(self.odb), hashes[str(self.odb)])

    def test_cannot_seal_already_stale_recipe(self):
        self.recipe.write_text('changed before archival\n')
        with self.assertRaisesRegex(ValueError, 'inputs changed'):
            archive.archive(self.proof)

    def test_cannot_replace_prior_receipt(self):
        archive.archive(self.proof)
        with self.assertRaisesRegex(ValueError, 'preserve'):
            archive.archive(self.proof)


if __name__ == '__main__':
    unittest.main()
