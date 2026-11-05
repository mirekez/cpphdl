"""Guard explicit receiver timing-mapping units and invalid targets."""
import unittest
import json
from pathlib import Path
import tempfile
from unittest.mock import patch
from receiver_mapped import timing_mapping, coarse_checkpoint, digest


class MappingOptions(unittest.TestCase):
    def test_coarse_reuse_binds_original_driver_and_all_design_inputs(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            source, design = root / 'driver.py', root / 'design.v'
            source.write_text('original driver\n')
            design.write_text('original design\n')
            (root / 'receiver_mapped.py').write_bytes(source.read_bytes())
            (root / 'coarse.rtlil').write_text('original intermediate\n')
            (root / 'synthesis-coarse.ys').write_text('original program\n')
            proof = dict(module='PCSReceiver', source_sha256={str(p): digest(p) for p in (source, design)},
                         synthesis_checkpoints={'coarse.rtlil': digest(root / 'coarse.rtlil')})
            (root / 'hardware_checks.json').write_text(json.dumps(proof))
            source.write_text('driver with new memory controls\n')
            with patch('receiver_mapped.SOURCE', source):
                self.assertEqual(coarse_checkpoint(root, 'original program\n'), root / 'coarse.rtlil')
                with self.assertRaisesRegex(ValueError, 'program changed'):
                    coarse_checkpoint(root, 'different synthesis program\n')
                design.write_text('changed design\n')
                with self.assertRaisesRegex(ValueError, 'input changed'):
                    coarse_checkpoint(root, 'original program\n')
                design.write_text('original design\n')
                (root / 'receiver_mapped.py').write_text('wrong historical driver\n')
                with self.assertRaisesRegex(ValueError, 'input changed'):
                    coarse_checkpoint(root, 'original program\n')
                (root / 'coarse.rtlil').write_text('changed intermediate\n')
                with self.assertRaisesRegex(ValueError, 'checkpoint changed'):
                    coarse_checkpoint(root, 'original program\n')

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
