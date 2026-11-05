"""Keep a failed placement's valid floorplan without accepting stale inputs."""
import json
from pathlib import Path
import sys
import tempfile
import unittest

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))
from unified_checkpoint import seal
from unified_synthesis import digest


class FloorplanCheckpoint(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory()
        self.addCleanup(self.temp.cleanup)
        self.work = Path(self.temp.name)
        for name in ('design.v', 'unified_place.tcl', 'floorplan.odb',
                     'constraints.sdc', 'route_tech.lef'):
            (self.work / name).write_text(name)
        (self.work / 'inventory.json').write_text(json.dumps(dict(
            movable_standard_cells=10, sram_macros=8, fixed_logic_modules=0)))
        self.proof = self.work / 'checks.json'
        self.report = dict(status='fail', error='memory limit', full_chip_qualified=False,
                           source_sha256={str(self.work / n): digest(self.work / n)
                                          for n in ('design.v', 'unified_place.tcl')})
        self.save()

    def save(self):
        self.proof.write_text(json.dumps(self.report))

    def test_preserves_checkpoint_and_separates_recipe(self):
        result = json.loads(seal(self.proof).read_text())
        self.assertEqual(result['status'], 'floorplanned_unplaced')
        self.assertFalse(result['full_chip_qualified'])
        self.assertNotIn('error', result)
        self.assertIn(str(self.work / 'design.v'), result['source_sha256'])
        self.assertIn(str(self.work / 'unified_place.tcl'), result['recipe_sha256'])
        self.assertEqual(result['artifact_sha256'][str(self.work / 'floorplan.odb')],
                         digest(self.work / 'floorplan.odb'))
        with self.assertRaisesRegex(ValueError, 'already exists'):
            seal(self.proof)

    def test_reject_changed_design(self):
        (self.work / 'design.v').write_text('different circuit')
        with self.assertRaisesRegex(ValueError, 'inputs changed'):
            seal(self.proof)

    def test_reject_live_writer(self):
        self.report['status'] = 'running'
        self.save()
        with self.assertRaisesRegex(ValueError, 'must have stopped'):
            seal(self.proof)


if __name__ == '__main__':
    unittest.main()
