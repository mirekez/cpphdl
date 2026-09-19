import struct
import unittest

from capture_regfile import decode_trace


class RegfileTraceTests(unittest.TestCase):
    def setUp(self):
        self.reset = struct.pack("<QQHHBBH", 0, 0, 0, 0, 0, 0, 0)
        self.work = struct.pack("<QQHHBBH", 0x123456789abcdef0, 0xfedcba9876543210,
                                1023, 1023, 3, 1, 0)

    def test_valid_trace(self):
        records = decode_trace(self.reset + self.work, 2)
        self.assertEqual(records[1], (0x123456789abcdef0, 0xfedcba9876543210,
                                      1023, 1023, 3, 1, 0))

    def test_truncation(self):
        with self.assertRaises(ValueError):
            decode_trace((self.reset + self.work)[:-1], 2)

    def test_wrong_cycle_count(self):
        with self.assertRaises(ValueError):
            decode_trace(self.reset + self.work, 1)

    def test_empty_trace(self):
        with self.assertRaises(ValueError):
            decode_trace(b"", 0)

    def test_initial_reset_required(self):
        with self.assertRaises(ValueError):
            decode_trace(self.work, 1)

    def test_field_bounds(self):
        for field, value in [(2, 1024), (3, 1024), (4, 4), (5, 2), (6, 1)]:
            with self.subTest(field=field):
                record = list(struct.unpack("<QQHHBBH", self.work))
                record[field] = value
                with self.assertRaises(ValueError):
                    decode_trace(self.reset + struct.pack("<QQHHBBH", *record), 2)


if __name__ == "__main__":
    unittest.main()
