"""Do not silently preload an unrecorded library into physical tools."""
from pathlib import Path
import sys
import tempfile
import unittest

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))
from receiver_allocator import configuration, environment, CONFIGURATION


class Allocator(unittest.TestCase):
    def test_system_mode_removes_inherited_injection(self):
        selected, record, files = configuration(None)
        self.assertEqual(files, [])
        self.assertEqual(record['implementation'], 'system')
        self.assertEqual(environment({'LD_PRELOAD': 'unknown.so', 'MALLOC_CONF': 'unknown', 'PATH': 'x'}, selected),
                         {'PATH': 'x'})

    def test_explicit_library_is_reported(self):
        with tempfile.TemporaryDirectory() as temporary:
            path = Path(temporary) / 'libjemalloc.so.2'
            path.write_bytes(b'fixture')
            selected, record, files = configuration(path)
            self.assertEqual(files, [path.resolve()])
            self.assertEqual(record['library'], selected['LD_PRELOAD'])
            self.assertEqual(environment({'MALLOC_CONF': 'unknown'}, selected)['MALLOC_CONF'], CONFIGURATION)
            bad = path.with_name('library with spaces.so')
            bad.touch()
            with self.assertRaises(ValueError):
                configuration(bad)
            with self.assertRaises(FileNotFoundError):
                configuration(path.with_name('missing.so'))


if __name__ == '__main__':
    unittest.main()
