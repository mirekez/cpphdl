"""A well-mask change must run new DRC and extraction before comparison."""
from contextlib import nullcontext
import json
from pathlib import Path
from types import SimpleNamespace
import sys
import tempfile
import unittest
from unittest.mock import patch

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))
import qualify_row_end_wells as flow


class RepairedWellFlow(unittest.TestCase):
    def setUp(self):
        temporary = tempfile.TemporaryDirectory()
        self.addCleanup(temporary.cleanup)
        root = Path(temporary.name)
        self.args = SimpleNamespace(candidate=root / 'candidate', retained=root / 'old',
            comparison=root / 'old-proof.json', output=root / 'qualification',
            pdk=root / 'pdk', tools=root / 'tools')
        fixture = self.args.retained / 'fixture'
        fixture.mkdir(parents=True)
        deck = fixture / 'pdk/ihp-sg13g2/libs.tech/klayout/tech/lvs'
        deck.mkdir(parents=True)
        (deck / 'run_lvs.py').write_text('fixture extraction tool')
        original = fixture / 'assembly.gds'
        original.write_bytes(b'original geometry')
        (fixture / 'core.cdl').write_text('retained connectivity')
        reference = root / 'sram.cir'
        reference.write_text('retained SRAM reference')
        (fixture / 'preparation.json').write_text(json.dumps({'sram_reference': str(reference)}))
        old_database = root / 'old.lvsdb'
        old_database.write_text('original graph pass')
        self.args.comparison.write_text(json.dumps(dict(status='assembly_graph_correspondence_pass',
            source_sha256={str(original): flow.digest(original)},
            artifact_sha256={str(old_database): flow.digest(old_database)})))
        self.args.candidate.mkdir()
        candidate = self.args.candidate / 'assembly.gds'
        candidate.write_bytes(b'repaired well geometry')
        (self.args.candidate / 'checks.json').write_text(json.dumps(dict(status='candidate_generated',
            modified_layers=[[31, 0]], source_sha256={str(original): flow.digest(original)},
            artifact_sha256={str(candidate): flow.digest(candidate)})))
        self.called = []

    def execute(self, command, **options):
        command = list(map(str, command))
        stage = Path(options['stdout'].name).stem
        self.called.append(stage)
        if stage == self.failure:
            return SimpleNamespace(returncode=1)
        if stage in ('drc', 'extraction'):
            flag = '--path' if stage == 'drc' else '--layout'
            self.assertEqual(Path(command[command.index(flag) + 1]).read_bytes(), b'repaired well geometry')
            output = Path(command[command.index('--run_dir') + 1])
            output.mkdir()
            name = 'assembly_FlowControlledRepeater_full.lyrdb' if stage == 'drc' else 'assembly.lvsdb'
            (output / name).write_text('fresh ' + stage)
        else:
            producer = Path(command[command.index('--retained') + 1])
            stages = json.loads((producer / 'checks.json').read_text())['stages']
            self.assertTrue(any(s['name'] == 'extraction' and s['returncode'] == 0 for s in stages))
            extraction = json.loads((producer / 'extraction_receipt.json').read_text())
            database = producer / 'lvs/assembly.lvsdb'
            self.assertEqual(extraction['artifact_sha256'][str(database)], flow.digest(database))
            self.producer_receipt = producer / 'checks.json'
            self.producer_hash = flow.digest(self.producer_receipt)
            output = Path(command[command.index('--output') + 1])
            output.mkdir()
            (output / 'checks.json').write_text(json.dumps(dict(status='assembly_graph_correspondence_pass',
                assembly_audit={'status': 'pass'})))
        return SimpleNamespace(returncode=0)

    def run_flow(self, failure=None):
        self.failure = failure
        with patch.object(flow, 'memory_slot', return_value=nullcontext(True)), \
             patch.object(flow, 'execute', side_effect=self.execute), \
             patch.object(flow, 'check_drc', return_value={'violations': 0}), \
             patch.object(flow.shutil, 'disk_usage', return_value=SimpleNamespace(free=2 * 1024**3)):
            flow.run(self.args)

    def test_new_geometry_requires_all_stages_and_frozen_producer(self):
        self.run_flow()
        self.assertEqual(self.called, ['drc', 'extraction', 'correspondence'])
        self.assertEqual(flow.digest(self.producer_receipt), self.producer_hash)
        result = json.loads((self.args.output / 'checks.json').read_text())
        self.assertEqual(result['status'], 'assembly_geometry_pass')
        self.assertFalse(result['full_chip_qualified'])

    def test_drc_failure_stops_before_extraction(self):
        with self.assertRaisesRegex(ValueError, 'drc failed'):
            self.run_flow('drc')
        self.assertEqual(self.called, ['drc'])

    def test_failed_extraction_cannot_use_old_lvs(self):
        with self.assertRaisesRegex(ValueError, 'extraction failed'):
            self.run_flow('extraction')
        self.assertEqual(self.called, ['drc', 'extraction'])

    def test_failed_new_lvs_is_not_geometry_pass(self):
        with self.assertRaisesRegex(ValueError, 'correspondence failed'):
            self.run_flow('correspondence')
        result = json.loads((self.args.output / 'checks.json').read_text())
        self.assertEqual(result['status'], 'fail')


if __name__ == '__main__':
    unittest.main()
