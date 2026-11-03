#!/usr/bin/env python3
"""Negative and positive tests of the full-output native replacement gate."""
import copy
import gzip
import json
from pathlib import Path
import tempfile
import unittest

from native_reference import audit, compare, digest, qualify


class ReferenceContract(unittest.TestCase):
    def setUp(self):
        self.directory = tempfile.TemporaryDirectory()
        self.addCleanup(self.directory.cleanup)
        self.root = Path(self.directory.name)
        self.a, self.b = self.root / 'reference.dat', self.root / 'native.dat'
        self.columns = ['time_s', 'P', 'N']
        self.good = 'time_s P N\n0 1 2\n0.5 2 1\n1 3 0\n'
        self.a.write_text(self.good)
        self.b.write_text(self.good)

    def compare(self, **kw):
        return compare(self.a, self.b, self.columns, 1, **kw)

    def test_complete_exact_and_compressed(self):
        result = self.compare()
        self.assertEqual(result['status'], 'pass')
        self.assertTrue(result['bitwise_identical'])
        self.assertEqual(result['rows'], 3)
        packed = self.root / 'reference.dat.gz'
        with gzip.open(packed, 'wt') as stream:
            stream.write(self.good.replace('time_s P N', 'time observation0 observation1'))
        self.assertEqual(compare(packed, self.b, self.columns, 1)['status'], 'pass')

    def test_each_interior_sample_checked(self):
        self.b.write_text(self.good.replace('0.5 2 1', '0.5 2.0001 1'))
        self.assertEqual(self.compare()['status'], 'fail')
        result = self.compare(absolute=.001)
        self.assertEqual(result['status'], 'pass')
        self.assertFalse(result['bitwise_identical'])

    def test_invalid_traces(self):
        for bad in (
            'time_s P N\n0 1 2\n',
            self.good.replace('0.5 2 1', '0.5 nan 1'),
            self.good.replace('0.5 2 1', '0.5 2'),
            self.good.replace('0.5 2 1', '0.6 2 1'),
            self.good.replace('0.5 2 1', '-0.5 2 1'),
            self.good.replace('time_s P N', 'time_s N P'),
            self.good.replace('0 1 2', '.1 1 2'),
            self.good.replace('1 3 0', '0.9 3 0'),
        ):
            with self.subTest(trace=bad):
                self.b.write_text(bad)
                with self.assertRaises(ValueError): self.compare()

    def test_reference_truncation_and_duplicate_events(self):
        self.a.write_text('time_s P N\n0 1 2\n.5 2 1\n')
        self.b.write_text(self.a.read_text())
        with self.assertRaises(ValueError): self.compare()
        for path in (self.a, self.b):
            path.write_text(self.good.replace('0.5 2 1', '0.5 2 1\n0.5 1 2'))
        self.assertEqual(self.compare()['rows'], 4)
        self.b.write_text(self.good)
        with self.assertRaises(ValueError): self.compare()

    def test_selected_library_and_continuations(self):
        deck, lib, model = (self.root / name for name in ('bench.cir', 'corner.lib', 'model.lib'))
        model.write_text('.model psp psp103va\n+ level=1\n')
        lib.write_text('.lib tt\n.include model.lib\n.endl tt\n.lib ff\n.include missing.lib\n.endl ff\n')
        deck.write_text('.lib "corner.lib" tt\nR1 a 0 1k\n.control\nlet observation0 = v(a)\n'
                        'tran 1p 1n 0 1p uic\n.endc\n.end\n')
        report = audit(deck)
        self.assertEqual(report['lexical_device_declarations'], {'R': 1})
        self.assertEqual(report['model_types'], {'psp': 'psp103va'})
        self.assertEqual(len(report['input_sha256']), 3)
        self.assertEqual(len(report['observations']), 1)
        self.assertFalse(report['native_full_deck_supported'])
        deck.write_text('.lib corner.lib missing\n')
        with self.assertRaises(ValueError): audit(deck)
        deck.write_text('.include bench.cir\n')
        with self.assertRaises(ValueError): audit(deck)

    def test_speed_cannot_hide_inaccuracy_or_missing_checks(self):
        deck = self.root / 'bench.cir'
        deck.write_text('* fixture, not an actual measured 1G run\n')
        reference = dict(engine='ngspice', status='complete', input_sha256={str(deck): digest(deck)},
                         columns=self.columns, stop_s=1, numerical_contract={'method': 'gear'},
                         host={'cpu': 'unit-test fixture'}, trace=str(self.a), trace_sha256=digest(self.a),
                         checks={'independent_contract': True},
                         timing={'scope': 'simulation_and_output', 'threads': 1, 'wall_s': 1000})
        candidate = copy.deepcopy(reference)
        candidate.update(engine='cpphdl', trace=str(self.b), trace_sha256=digest(self.b))
        candidate['timing']['wall_s'] = 10
        rp, cp = self.root / 'ngspice.json', self.root / 'cpphdl.json'
        def run():
            rp.write_text(json.dumps(reference))
            cp.write_text(json.dumps(candidate))
            return qualify(rp, cp)
        self.assertEqual(run()['status'], 'pass')
        candidate['timing']['wall_s'] = 11
        self.assertEqual(run()['status'], 'fail')
        candidate['timing']['wall_s'] = 1
        candidate['checks']['independent_contract'] = False
        with self.assertRaises(ValueError): run()
        candidate['checks']['independent_contract'] = True
        self.b.write_text(self.good.replace('0.5 2 1', '0.5 2.0001 1'))
        with self.assertRaises(ValueError): run()  # Cannot reuse a stale trace hash.
        candidate['trace_sha256'] = digest(self.b)
        self.assertEqual(run()['status'], 'fail')
        deck.write_text('* changed inputs\n')
        with self.assertRaises(ValueError): run()


if __name__ == '__main__':
    unittest.main()
