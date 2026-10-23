"""DRC reuse must preserve geometry, rules and independently checked evidence."""
import hashlib
import json
from pathlib import Path
import sys
import tempfile
import unittest
from unittest.mock import patch

SOURCE = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(SOURCE))
import verify_physical


def digest(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


class DRCReuse(unittest.TestCase):
    def setUp(self):
        self.temporary = tempfile.TemporaryDirectory()
        self.addCleanup(self.temporary.cleanup)
        self.work = Path(self.temporary.name)
        self.pdk = self.work / 'pdk'
        revision = self.pdk / '.cpphdl/revision.txt'
        revision.parent.mkdir(parents=True)
        revision.write_text('fixture-revision\n')
        tech = self.pdk / 'ihp-sg13g2/libs.tech/klayout/tech'
        self.rule = tech / 'drc/rule_decks/geometry/fixture.drc'
        self.rule.parent.mkdir(parents=True)
        self.rule.write_text('fixture rules\n')
        self.layout = self.work / 'pad.gds'
        self.layout.write_bytes(b'independent geometry fixture')
        self.raw = self.work / 'pad_full.lyrdb'
        self.raw.write_text('fixture database; parser mocked separately')
        self.stats = dict(violations=0, report=str(self.raw), categories=869)
        self.prior = dict(status='failed', top='RepeaterPadframe', layout=str(self.layout),
                          pdk_revision='fixture-revision', checks=dict(drc=self.stats),
                          input_sha256={str(path): digest(path) for path in
                                        (self.layout, self.rule, SOURCE / 'checks.py')})
        self.evidence = self.work / 'physical_checks.json'
        self.parser = patch.object(verify_physical, 'check_drc', return_value=self.stats.copy())
        self.mock = self.parser.start()
        self.addCleanup(self.parser.stop)

    def reuse(self, top='RepeaterPadframe'):
        self.evidence.write_text(json.dumps(self.prior))
        return verify_physical.reuse_drc(self.evidence, self.layout, self.pdk, top)

    def test_drc_pass_can_be_reused_after_independent_lvs_failure(self):
        stats, provenance, hashes = self.reuse()
        self.assertEqual(stats, self.stats)
        self.assertEqual(provenance['origin_sha256'], digest(self.evidence))
        self.assertEqual(hashes[str(self.raw)], digest(self.raw))
        self.mock.assert_called_once()

    def test_changed_geometry_top_revision_rules_or_checker_fail(self):
        for mutation in ('layout', 'top', 'revision', 'rule', 'checker'):
            with self.subTest(mutation=mutation):
                old = json.loads(json.dumps(self.prior))
                if mutation == 'layout':
                    self.prior['input_sha256'][str(self.layout)] = 'wrong'
                elif mutation == 'top':
                    self.prior['top'] = 'OtherTop'
                elif mutation == 'revision':
                    self.prior['pdk_revision'] = 'older'
                else:
                    path = self.rule if mutation == 'rule' else SOURCE / 'checks.py'
                    self.prior['input_sha256'][str(path)] = 'wrong'
                with self.assertRaises(ValueError):
                    self.reuse()
                self.prior = old

    def test_absent_pass_or_changed_statistics_fail(self):
        self.prior['checks'] = {}
        with self.assertRaises(ValueError):
            self.reuse()
        self.prior['checks'] = dict(drc=dict(self.stats, categories=870))
        with self.assertRaises(ValueError):
            self.reuse()

    def test_raw_reports_are_reparsed_and_incomplete_evidence_fails(self):
        self.mock.side_effect = ValueError('density did not complete')
        with self.assertRaisesRegex(ValueError, 'density did not complete'):
            self.reuse()


if __name__ == '__main__':
    unittest.main()
