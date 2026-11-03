"""Packaging must reject changed geometry, sources and recorded trajectories."""
import copy
import json
from pathlib import Path
import sys
import tempfile
import unittest

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))
from package_clock_results import digest, verify_implementation


class PackageEvidence(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory()
        self.addCleanup(self.temp.cleanup)
        self.work = Path(self.temp.name)
        for name, data in [('source.cpp', 'original source'), ('cpp_trace.csv', '1,2\n'),
                           ('postroute_trace.csv', '1,2\n'), ('module_filled.gds', 'layout')]:
            (self.work / name).write_text(data)
        native = self.work / 'cpp_trace.csv'
        self.proof = dict(status='component_checks_pass', top='ProcessingClockControl',
            native_postroute_exact=True, full_1000base_t_chip=False,
            input_sha256={str(self.work / 'source.cpp'): digest(self.work / 'source.cpp'),
                          str(native): digest(native)},
            retained_input_paths={str(native): str(native)},
            artifact_sha256={'module_filled.gds': digest(self.work / 'module_filled.gds')})

    def check(self, proof=None):
        (self.work / 'physical_module.json').write_text(json.dumps(proof or self.proof))
        return verify_implementation(self.work)

    def test_complete_original_evidence(self):
        self.assertEqual(self.check()[0], self.proof)

    def test_failed_or_wrong_scope(self):
        for key, value in [('status', 'running'), ('top', 'PCSReceiver'),
                           ('native_postroute_exact', False), ('full_1000base_t_chip', True)]:
            with self.subTest(key=key), self.assertRaises(ValueError):
                self.check(dict(self.proof, **{key: value}))

    def test_changed_layout(self):
        (self.work / 'module_filled.gds').write_text('changed')
        with self.assertRaisesRegex(ValueError, 'artifact changed'):
            self.check()

    def test_changed_source(self):
        (self.work / 'source.cpp').write_text('changed')
        with self.assertRaisesRegex(ValueError, 'input unavailable'):
            self.check()

    def test_changed_replay(self):
        (self.work / 'postroute_trace.csv').write_text('1,3\n')
        with self.assertRaisesRegex(ValueError, 'trajectories differ'):
            self.check()

    def test_unbound_gds(self):
        proof = copy.deepcopy(self.proof)
        proof['artifact_sha256'] = {'source.cpp': digest(self.work / 'source.cpp')}
        with self.assertRaisesRegex(ValueError, 'filled GDS is not bound'):
            self.check(proof)

    def test_unbound_native(self):
        proof = copy.deepcopy(self.proof)
        proof['retained_input_paths'] = {}
        with self.assertRaisesRegex(ValueError, 'trajectories differ or are unbound'):
            self.check(proof)


if __name__ == '__main__':
    unittest.main()
