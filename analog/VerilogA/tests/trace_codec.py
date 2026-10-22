"""A time codec must not silently discard a physical event or alter data."""
import gzip
import importlib.util
from pathlib import Path
import tempfile
import unittest
import json
import sys

spec = importlib.util.spec_from_file_location('spice_trace_codec', Path(__file__).resolve().parents[1] / 'trace_codec.py')
codec = importlib.util.module_from_spec(spec)
spec.loader.exec_module(codec)


class Codec(unittest.TestCase):
    def test_explicit_roundoff_bound_is_recorded_and_cannot_accumulate(self):
        with tempfile.TemporaryDirectory() as temporary:
            path = Path(temporary) / 'trace.dat'
            base = b'time v(a)\n0 0\n1e-9 0\n'
            path.write_bytes(base + f'1e-9 {48 * sys.float_info.epsilon:.17g}\n'.encode())
            with self.assertRaises(ValueError):
                codec.normalize_roundoff_duplicates(path)
            audit = codec.normalize_roundoff_duplicates(path, epsilon_units=64)
            self.assertEqual(json.loads(audit.read_text())['epsilon_units'], 64)
            self.assertEqual(codec.verify_roundoff_audit(path, audit), 1)
            path.write_bytes(base + f'1e-9 {65 * sys.float_info.epsilon:.17g}\n'.encode())
            with self.assertRaises(ValueError):
                codec.normalize_roundoff_duplicates(path, epsilon_units=64)
            for value in (0, 65, 16.0, True):
                with self.assertRaises(ValueError):
                    codec.normalize_roundoff_duplicates(path, epsilon_units=value)

    def test_audit_reconstructs_every_original_byte_after_compression(self):
        text = b'time v(a)\n0 0\n1e-9 1\n1e-9 1.000000000000001\n2e-9 2\n2e-9 2\n'
        with tempfile.TemporaryDirectory() as temporary:
            path = Path(temporary) / 'trace.dat'
            path.write_bytes(text)
            audit = codec.normalize_roundoff_duplicates(path)
            self.assertEqual(path.read_bytes(), b'time v(a)\n0 0\n1e-9 1\n2e-9 2\n')
            archive = path.with_suffix('.gz')
            with gzip.open(archive, 'wb') as file:
                file.write(path.read_bytes())
            self.assertEqual(codec.verify_roundoff_audit(archive, audit), 2)
            path.write_bytes(path.read_bytes().replace(b'2e-9 2', b'2e-9 3'))
            with self.assertRaises(ValueError):
                codec.verify_roundoff_audit(path, audit)

    def test_changed_state_backward_time_and_nonfinite_values_remain_errors(self):
        for tail in (b'1e-9 1.000001\n', b'0 1\n', b'1e-9 nan\n', b'1e-9 1 2\n'):
            with self.subTest(tail=tail), tempfile.TemporaryDirectory() as temporary:
                path = Path(temporary) / 'trace.dat'
                original = b'time v(a)\n0 0\n1e-9 1\n' + tail
                path.write_bytes(original)
                with self.assertRaises(ValueError):
                    codec.normalize_roundoff_duplicates(path)
                self.assertEqual(path.read_bytes(), original)
                self.assertFalse(path.with_name(path.name + '.roundoff.json').exists())

    def test_roundoff_cannot_accumulate_through_a_duplicate_chain(self):
        text = b'time v(a)\n0 0\n1e-9 1\n' + b''.join(f'1e-9 {1 + i * 2e-15:.17g}\n'.encode() for i in range(1, 10))
        with tempfile.TemporaryDirectory() as temporary:
            path = Path(temporary) / 'trace.dat'
            path.write_bytes(text)
            with self.assertRaises(ValueError):
                codec.normalize_roundoff_duplicates(path)
            self.assertEqual(path.read_bytes(), text)


if __name__ == '__main__':
    unittest.main()
