"""Critical output selection must exclude inputs, clocks and unrelated cells."""
from pathlib import Path
import copy
import json
import sys
import tempfile
import unittest

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))
from receiver_output_stage import select_outputs, unbuffer_origin, digest


class Outputs(unittest.TestCase):
    def test_real_report_rows_and_duplicates(self):
        row = '1 0.016947195 0.414285362 0.447397232 6.349400520 ^ _105946_/Y (sg13g2_a221oi_1)\n'
        self.assertEqual(select_outputs(row + row), ['_105946_/Y'])
        self.assertEqual(select_outputs(row + row.replace('_105946_', '_105947_'), 1), ['_105946_/Y'])
        self.assertEqual(select_outputs('1 0.02 0.4 0.4 6.3 ^ xor_gate/X (sg13g2_xor2_1)'), ['xor_gate/X'])

    def test_only_loaded_slow_single_strength_outputs(self):
        rows = '''1 0.016 0.400 0.4 6.3 ^ clkbuf_1/X (sg13g2_buf_8)
1 0.006 0.400 0.4 6.3 ^ light/Y (sg13g2_a221oi_1)
1 0.016 0.100 0.4 6.3 ^ fast/Y (sg13g2_a221oi_1)
  0.400 0.4 6.3 ^ input/A1 (sg13g2_a221oi_1)
1 0.016 0.400 0.4 6.3 ^ strong/Y (sg13g2_nor2_2)
'''
        self.assertEqual(select_outputs(rows), [])

    def test_bounds_and_tcl_identifiers(self):
        for limit in (0, 129, True, 1.5):
            with self.subTest(limit=limit), self.assertRaises(ValueError):
                select_outputs('', limit)
        with self.assertRaisesRegex(ValueError, 'identifier'):
            select_outputs('1 0.02 0.4 0.4 6.3 ^ evil;cmd/Y (sg13g2_a221oi_1)')


class DiagnosticOrigin(unittest.TestCase):
    def setUp(self):
        self.temporary = tempfile.TemporaryDirectory()
        self.addCleanup(self.temporary.cleanup)
        self.origin = Path(self.temporary.name) / 'origin'
        self.measurement = Path(self.temporary.name) / 'measurement'
        self.origin.mkdir()
        self.measurement.mkdir()
        for name in ('candidate.odb', 'candidate.sdc', 'after.v'):
            (self.origin / name).write_text(name)
        self.proof = dict(status='diagnostic_complete', rounds=[dict(name='after',
            setup_ns=.2, hold_ns=.13, electrical_pass=True,
            connectivity=dict(status='pass', top='PCSReceiver'))], source_sha256={},
            artifact_sha256={str(p): digest(p) for p in self.origin.iterdir()})
        (self.origin / 'checks.json').write_text(json.dumps(self.proof))
        for name in ('before', 'margin', 'restored'):
            for kind in ('setup', 'hold'):
                value = .2 if kind == 'setup' else .13
                if name == 'margin' and kind == 'setup':
                    value = -.8
                mode = 'max' if kind == 'setup' else 'min'
                (self.measurement / (name + '_' + kind + '.rpt')).write_text(
                    'worst slack ' + mode + ' ' + str(value))
        for name in ('reloaded.v', 'margin_paths.rpt'):
            (self.measurement / name).write_text(name)
        self.measured = dict(status='global_margin_measurement_complete', stage='complete',
            connectivity=dict(status='pass', top='PCSReceiver', removed_buffers=0, removed_fillers=0),
            input_sha256={str(p): digest(p) for p in self.origin.iterdir()},
            artifact_sha256={str(p): digest(p) for p in self.measurement.iterdir()},
            estimates={name: dict(setup_ns=.2, hold_ns=.13, electrical_pass=True)
                       for name in ('before', 'restored')})

    def check(self, measured=None):
        (self.measurement / 'checks.json').write_text(json.dumps(measured or self.measured))
        return unbuffer_origin(self.origin, self.measurement, self.proof)

    def test_failed_margin_can_supply_repair(self):
        hashes = self.check()
        self.assertEqual(hashes[str(self.origin / 'candidate.odb')], digest(self.origin / 'candidate.odb'))

    def test_incomplete_or_changed_topology_rejected(self):
        for key, value in (('status', 'running'), ('stage', 'measure')):
            measured = copy.deepcopy(self.measured)
            measured[key] = value
            with self.subTest(key=key), self.assertRaises(ValueError):
                self.check(measured)
        for key in ('removed_buffers', 'removed_fillers'):
            measured = copy.deepcopy(self.measured)
            measured['connectivity'][key] = 1
            with self.subTest(key=key), self.assertRaises(ValueError):
                self.check(measured)

    def test_unrelated_or_mutated_producer_rejected(self):
        measured = copy.deepcopy(self.measured)
        del measured['input_sha256'][str(self.origin / 'checks.json')]
        with self.assertRaises(ValueError):
            self.check(measured)
        (self.origin / 'candidate.odb').write_text('different candidate')
        with self.assertRaises(ValueError):
            self.check()

    def test_bad_nominal_restoration_rejected(self):
        measured = copy.deepcopy(self.measured)
        measured['estimates']['restored']['electrical_pass'] = False
        with self.assertRaises(ValueError):
            self.check(measured)
        measured = copy.deepcopy(self.measured)
        measured['estimates']['restored']['setup_ns'] += .01
        with self.assertRaises(ValueError):
            self.check(measured)


if __name__ == '__main__':
    unittest.main()
