#!/usr/bin/env python3
"""Reject stale or different experiments before reusing a completed case."""
import argparse
import gzip
import json
from pathlib import Path
import tempfile
import unittest

from core_transmit_states import copy_reusable
from transmit_driver import digest


class ReuseTest(unittest.TestCase):
    def fixture(self, directory):
        root = Path(directory)
        original, current = root / 'original', root / 'current'
        original.mkdir()
        current.mkdir()
        for name in ('design.va', 'bench.cir', 'conditions.json', 'columns.csv', 'interface.json', 'controls.csv'):
            (original / name).write_text(name + '\n')
            (current / name).write_text(name + '\n')
        source = root / 'device.hpp'
        source.write_text('physical device\n')
        binary = root / 'bench'
        binary.write_bytes(b'producer')
        wave = original / 'trace.csv.gz'
        with gzip.open(wave, 'wt') as stream:
            stream.write('time_s,V\n' + ''.join(f'{i},1\n' for i in range(10)))
        receipt = root / 'checks.json'
        report = dict(status='fail', error='[Errno 28] No space left on device', flow='cpp',
                      source_sha256={str(source): digest(source)}, binary_sha256={str(binary): digest(binary)},
                      cases={'nominal': dict(checks={'electrical': True}, waveform=str(wave),
                        waveform_sha256=digest(wave), controls_sha256=digest(original / 'controls.csv'),
                        interface_sha256=digest(original / 'interface.json'))})
        receipt.write_text(json.dumps(report))
        args = argparse.Namespace(reuse_report=receipt, flow='cpp', bench=binary)
        return args, original, current, source, report

    def test_identical_complete_case_can_be_rechecked(self):
        with tempfile.TemporaryDirectory() as directory:
            args, original, current, source, report = self.fixture(directory)
            reused = copy_reusable(args, 'nominal', current)
            self.assertEqual(digest(current / 'trace.csv.gz'), digest(original / 'trace.csv.gz'))
            self.assertEqual(reused['waveform_sha256'], digest(original / 'trace.csv.gz'))

    def test_changed_physics_deck_waveform_or_binary_rejected(self):
        for name in ('source', 'deck', 'waveform', 'binary'):
            with self.subTest(name=name), tempfile.TemporaryDirectory() as directory:
                args, original, current, source, report = self.fixture(directory)
                path = dict(source=source, deck=current / 'bench.cir', waveform=original / 'trace.csv.gz',
                            binary=args.bench)[name]
                path.write_bytes(b'changed')
                with self.assertRaises(ValueError):
                    copy_reusable(args, 'nominal', current)

    def test_electrical_failure_cannot_be_recovered_as_storage_failure(self):
        with tempfile.TemporaryDirectory() as directory:
            args, original, current, source, report = self.fixture(directory)
            report['cases']['nominal']['checks']['electrical'] = False
            args.reuse_report.write_text(json.dumps(report))
            with self.assertRaisesRegex(ValueError, 'physical requirements'):
                copy_reusable(args, 'nominal', current)

    def test_other_failure_cannot_be_recovered(self):
        with tempfile.TemporaryDirectory() as directory:
            args, original, current, source, report = self.fixture(directory)
            report['error'] = 'weighted level failed'
            args.reuse_report.write_text(json.dumps(report))
            with self.assertRaisesRegex(ValueError, 'another reason'):
                copy_reusable(args, 'nominal', current)


if __name__ == '__main__':
    unittest.main()
