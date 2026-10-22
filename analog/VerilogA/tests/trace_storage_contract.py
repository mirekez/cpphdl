"""Archival preserves every byte and never removes a failed original trace."""
import gzip
import hashlib
from pathlib import Path
import sys
import tempfile
import unittest
from unittest.mock import patch

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))
import trace_storage


class Storage(unittest.TestCase):
    def test_stream_capture_preserves_every_byte_without_a_raw_file(self):
        with tempfile.TemporaryDirectory() as temporary:
            source = Path(temporary) / 'trace.csv'
            data = b'time_s,Y\n0,1.2345678901234567\n' * 50000
            with trace_storage.capture_trace(source) as capture:
                with source.open('wb') as producer:
                    producer.write(data)
            with gzip.open(capture.path, 'rb') as restored:
                self.assertEqual(restored.read(), data)
            self.assertFalse(source.exists())
            self.assertEqual(capture.metadata['original_sha256'], hashlib.sha256(data).hexdigest())
            self.assertEqual(capture.metadata['original_bytes'], len(data))

    def test_failed_producer_releases_the_stream_reader(self):
        with tempfile.TemporaryDirectory() as temporary:
            source = Path(temporary) / 'trace.csv'
            with self.assertRaisesRegex(RuntimeError, 'producer failed'):
                with trace_storage.capture_trace(source) as capture:
                    raise RuntimeError('producer failed')
            self.assertFalse(source.exists())
            self.assertEqual(capture.metadata['original_bytes'], 0)

    def test_stream_capture_does_not_overwrite_existing_evidence(self):
        for name in ('trace.csv', 'trace.csv.gz'):
            with self.subTest(name=name), tempfile.TemporaryDirectory() as temporary:
                source = Path(temporary) / 'trace.csv'
                existing = Path(temporary) / name
                existing.write_bytes(b'previous evidence')
                with self.assertRaisesRegex(ValueError, 'overwrite'):
                    with trace_storage.capture_trace(source):
                        pass
                self.assertEqual(existing.read_bytes(), b'previous evidence')

    def test_complete_byte_stream_is_reconstructed(self):
        with tempfile.TemporaryDirectory() as temporary:
            source = Path(temporary) / 'spice.dat'
            data = b'time v(a)\n0 1.2345678901234567\n' * 50000
            source.write_bytes(data)
            archive, metadata = trace_storage.archive_trace(source)
            with gzip.open(archive, 'rb') as stream:
                self.assertEqual(stream.read(), data)
            self.assertEqual(metadata['original_sha256'], hashlib.sha256(data).hexdigest())
            self.assertFalse(source.exists())

    def test_existing_archive_is_preserved(self):
        with tempfile.TemporaryDirectory() as temporary:
            source = Path(temporary) / 'trace.csv'
            source.write_bytes(b'original')
            archive = source.with_name('trace.csv.gz')
            archive.write_bytes(b'previous archive')
            with self.assertRaisesRegex(ValueError, 'already exists'):
                trace_storage.archive_trace(source)
            self.assertEqual(source.read_bytes(), b'original')
            self.assertEqual(archive.read_bytes(), b'previous archive')

    def test_failed_reconstruction_retains_original(self):
        with tempfile.TemporaryDirectory() as temporary:
            source = Path(temporary) / 'trace.csv'
            source.write_bytes(b'original state')
            with patch.object(trace_storage.shutil, 'copyfileobj', side_effect=lambda src, dst: dst.write(b'damaged')):
                with self.assertRaisesRegex(ValueError, 'reconstruction'):
                    trace_storage.archive_trace(source)
            self.assertEqual(source.read_bytes(), b'original state')
            self.assertFalse(source.with_name('trace.csv.gz').exists())


if __name__ == '__main__':
    unittest.main()
