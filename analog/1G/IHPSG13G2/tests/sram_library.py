"""Prevent stale or incomplete SRAM verification from entering chip assembly."""
import copy
import json
from pathlib import Path
import sys
import tempfile
import unittest

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))
from sram_library import digest, verified_library


class LibraryTests(unittest.TestCase):
    def setUp(self):
        self.directory = tempfile.TemporaryDirectory()
        self.addCleanup(self.directory.cleanup)
        self.root = Path(self.directory.name)
        fixture = self.root / 'fixture'
        fixture.mkdir()
        (self.root / 'correspondence').mkdir()
        self.gds, self.reference = fixture / 'macro.gds', fixture / 'reference.cir'
        self.source, self.database = self.root / 'source.py', self.root / 'correspondence/macro.lvsdb'
        for path in (self.gds, self.reference, self.source, self.database):
            path.write_text(path.name)
        sources = {str(self.source): digest(self.source)}
        artifacts = {str(p): digest(p) for p in (self.gds, self.reference)}
        self.preparation = dict(status='prepared', fill={'added_shapes': 123},
                                source_sha256=sources, artifact_sha256=artifacts)
        audit = dict(status='pass', counts={'circuits': 39, 'devices': 7775})
        self.proof = dict(status='graph_correspondence_pass', independent_audit=audit,
                          source_sha256={**sources, **artifacts}, database=str(self.database),
                          database_sha256=digest(self.database))
        self.flow = dict(status='filled_graph_correspondence_pass', stage='complete',
                         independent_audit=copy.deepcopy(audit), source_sha256={**sources, **artifacts},
                         correspondence_receipt=str(self.root / 'correspondence/checks.json'))
        self.receipt = self.root / 'checks.json'
        self.save()

    def save(self):
        for path, value in ((self.receipt, self.flow), (self.root / 'correspondence/checks.json', self.proof),
                            (self.root / 'fixture/preparation.json', self.preparation)):
            path.write_text(json.dumps(value))

    def test_exact_pair_and_no_full_chip_claim(self):
        result = verified_library(self.receipt)
        self.assertEqual(result['gds'], self.gds)
        self.assertEqual(result['reference'], self.reference)
        self.assertFalse(result['full_sram_qualified'])
        self.assertFalse(result['full_chip_qualified'])
        self.assertIn(str(self.database), result['source_sha256'])

    def test_unfinished_flow(self):
        self.flow['stage'] = 'extraction'
        self.save()
        with self.assertRaisesRegex(ValueError, 'completed filled'):
            verified_library(self.receipt)

    def test_failed_independent_audit(self):
        self.proof['independent_audit']['status'] = 'fail'
        self.save()
        with self.assertRaisesRegex(ValueError, 'independent SRAM audit'):
            verified_library(self.receipt)

    def test_changed_sources_layout_reference_or_database(self):
        for path in (self.source, self.gds, self.reference, self.database):
            with self.subTest(path=path.name):
                original = path.read_bytes()
                path.write_bytes(original + b'changed')
                with self.assertRaisesRegex(ValueError, 'changed'):
                    verified_library(self.receipt)
                path.write_bytes(original)

    def test_conflicting_provenance(self):
        self.preparation['artifact_sha256'][str(self.gds)] = '0' * 64
        self.save()
        with self.assertRaisesRegex(ValueError, 'conflicting SRAM provenance'):
            verified_library(self.receipt)

    def test_unbound_layout(self):
        del self.flow['source_sha256'][str(self.gds)]
        self.save()
        with self.assertRaisesRegex(ValueError, 'not bound'):
            verified_library(self.receipt)

    def test_other_flow_correspondence(self):
        self.flow['correspondence_receipt'] = str(self.root / 'another/checks.json')
        self.save()
        with self.assertRaisesRegex(ValueError, 'belong to the supplied flow'):
            verified_library(self.receipt)

    def test_unfilled_layout(self):
        self.preparation['fill'] = None
        self.save()
        with self.assertRaisesRegex(ValueError, 'filled SRAM preparation'):
            verified_library(self.receipt)


if __name__ == '__main__':
    unittest.main()
