"""Archival preserves every byte and never removes a failed original trace."""
import gzip
import hashlib
import os
from pathlib import Path
import sys
import tempfile
import time
import unittest
from unittest.mock import patch

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))
import trace_storage


class Storage(unittest.TestCase):
    def test_completed_producer_waits_for_slow_archive_verification(self):
        # A large trace can finish producing before its full reconstruction
        # check finishes. Exceed the former five-second FIFO release grace
        # plus the 30-second completion cutoff.
        with tempfile.TemporaryDirectory() as temporary:
            source = Path(temporary) / 'trace.csv'
            data = b'time_s,V\n0,1\n1,2\n'
            original_digest = trace_storage.stream_digest
            calls = 0

            def slow_reconstruction(stream):
                nonlocal calls
                calls += 1
                if calls == 1:
                    time.sleep(36)
                return original_digest(stream)

            with patch.object(trace_storage, 'stream_digest', slow_reconstruction):
                with trace_storage.capture_trace(source) as capture:
                    source.write_bytes(data)
            self.assertEqual(capture.metadata['original_sha256'], hashlib.sha256(data).hexdigest())
            self.assertEqual(capture.metadata['original_bytes'], len(data))
            self.assertFalse(source.exists())
            with gzip.open(capture.path, 'rb') as restored:
                self.assertEqual(restored.read(), data)

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

    def test_scratch_capture_preserves_logical_path_and_all_bytes(self):
        with tempfile.TemporaryDirectory() as directory, tempfile.TemporaryDirectory() as scratch:
            source = Path(directory) / 'trace.csv'
            data = b'time_s,V\n0,1\n1,2\n' * 50000
            with patch.dict(os.environ, {'CPPHDL_ANALOG_TRACE_SCRATCH': scratch}):
                with trace_storage.capture_trace(source) as capture:
                    self.assertTrue(capture.path.is_symlink())
                    self.assertEqual(capture.path.resolve().parent, Path(scratch).resolve())
                    with source.open('wb') as producer:
                        producer.write(data)
            self.assertFalse(source.exists())
            with gzip.open(capture.path, 'rb') as stream:
                self.assertEqual(stream.read(), data)
            self.assertEqual(capture.metadata['original_sha256'], hashlib.sha256(data).hexdigest())
            self.assertEqual(capture.metadata['compression_level'], 6)

    def test_scratch_archival_and_failed_reconstruction_cleanup(self):
        with tempfile.TemporaryDirectory() as directory, tempfile.TemporaryDirectory() as scratch:
            source = Path(directory) / 'trace.csv'
            source.write_bytes(b'complete original state')
            with patch.dict(os.environ, {'CPPHDL_ANALOG_TRACE_SCRATCH': scratch}):
                with patch.object(trace_storage.shutil, 'copyfileobj', side_effect=lambda src, dst: dst.write(b'damaged')):
                    with self.assertRaisesRegex(ValueError, 'reconstruction'):
                        trace_storage.archive_trace(source)
                self.assertEqual(list(Path(scratch).iterdir()), [])
                self.assertEqual(source.read_bytes(), b'complete original state')
                archive, metadata = trace_storage.archive_trace(source)
            self.assertTrue(archive.is_symlink())
            self.assertEqual(archive.resolve().parent, Path(scratch).resolve())
            with gzip.open(archive, 'rb') as stream:
                self.assertEqual(stream.read(), b'complete original state')

    def test_dangling_previous_evidence_is_never_overwritten(self):
        with tempfile.TemporaryDirectory() as directory:
            source = Path(directory) / 'trace.csv'
            archive = source.with_name('trace.csv.gz')
            archive.symlink_to(Path(directory) / 'missing-archive')
            with self.assertRaisesRegex(ValueError, 'overwrite'):
                with trace_storage.capture_trace(source):
                    pass
            source.write_bytes(b'original')
            with self.assertRaisesRegex(ValueError, 'already exists'):
                trace_storage.archive_trace(source)
            self.assertTrue(archive.is_symlink())


if __name__ == '__main__':
    unittest.main()
