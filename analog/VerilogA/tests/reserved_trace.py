#!/usr/bin/env python3
"""Real filesystem/FIFO checks for reserved full-waveform storage."""
import errno
import gzip
import hashlib
from pathlib import Path
import sys
import tempfile
import unittest
from unittest.mock import patch

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))
from reserved_trace import capture_reserved_trace


class ReservedTrace(unittest.TestCase):
    def test_reservation_precedes_producer_and_preserves_every_byte(self):
        with tempfile.TemporaryDirectory() as directory:
            path = Path(directory) / 'trace.dat'
            content = b'time value\n' + b'1.0000000000000001e-9 1.2345678901234567\n' * 10000
            capacity = 2 << 20
            with capture_reserved_trace(path, capacity) as trace:
                self.assertEqual(trace.storage.stat().st_size, capacity)
                self.assertGreaterEqual(trace.storage.stat().st_blocks * 512, capacity)
                with path.open('wb') as output:
                    output.write(content)
            self.assertFalse(path.exists())
            self.assertLess(trace.storage.stat().st_size, capacity)
            with gzip.open(trace.path, 'rb') as source:
                self.assertEqual(source.read(), content)
            self.assertEqual(trace.metadata['original_sha256'], hashlib.sha256(content).hexdigest())
            self.assertEqual(trace.metadata['original_bytes'], len(content))

    def test_failed_reservation_never_enters_producer(self):
        with tempfile.TemporaryDirectory() as directory:
            path = Path(directory) / 'trace.dat'
            with patch('reserved_trace.os.posix_fallocate', side_effect=OSError(errno.ENOSPC, 'no space')):
                with self.assertRaises(OSError):
                    with capture_reserved_trace(path, 1024):
                        self.fail('producer entered without reserved disk blocks')
            self.assertFalse(path.exists())
            self.assertFalse(path.with_suffix('.dat.gz').exists())

    def test_capacity_exhaustion_is_failure_and_releases_reservation(self):
        with tempfile.TemporaryDirectory() as directory:
            path = Path(directory) / 'trace.dat'
            with self.assertRaises(OSError):
                with capture_reserved_trace(path, 20) as trace:
                    with path.open('wb') as output:
                        output.write(b'nonempty waveform')
            self.assertIsNone(trace.metadata)
            self.assertLessEqual(trace.storage.stat().st_size, 20)
            self.assertFalse(path.exists())


if __name__ == '__main__':
    unittest.main()
