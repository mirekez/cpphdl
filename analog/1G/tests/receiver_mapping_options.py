"""Guard explicit receiver timing-mapping units and invalid targets."""
import unittest
from receiver_mapped import timing_mapping


class MappingOptions(unittest.TestCase):
    def test_legacy_mapping_is_unchanged_without_a_target(self):
        self.assertIsNone(timing_mapping(None, 15.0))

    def test_explicit_units_and_driver(self):
        result = timing_mapping(3000.0, 15.0)
        self.assertEqual(result['delay_target_ps'], 3000.0)
        self.assertEqual(result['output_load_ff'], 15.0)
        self.assertEqual(result['driving_cell'], 'sg13g2_buf_4')

    def test_reject_nonphysical_values(self):
        for value in (0, -1, float('nan'), float('inf'), True):
            for delay, load in ((value, 15.0), (3000.0, value)):
                with self.subTest(delay=delay, load=load), self.assertRaises(ValueError):
                    timing_mapping(delay, load)


if __name__ == '__main__':
    unittest.main()
