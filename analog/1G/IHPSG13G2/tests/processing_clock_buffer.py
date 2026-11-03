"""Keep buffer polarity, physical dimensions and the receiver load explicit."""
from pathlib import Path
import sys
import unittest

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))
from processing_clock_buffer import buffer, connect

CDL = '''.SUBCKT sg13g2_buf_16 X A VDD VSS
MN0 net1 A VSS VSS sg13_lv_nmos m=1 w=4.44u l=130.00n ng=6
MN1 X net1 VSS VSS sg13_lv_nmos m=1 w=11.84u l=130.00n ng=16
MP1 X net1 VDD VDD sg13_lv_pmos m=1 w=17.92u l=130.00n ng=16
MP0 net1 A VDD VDD sg13_lv_pmos m=1 w=6.72u l=130.00n ng=6
.ENDS
'''
DECK = '''Cload OUT 0 200f IC=0
Xgate VDD 0 CLOCK ENABLE OUT ExtractedAnalog
.save v(CLOCK) v(ENABLE) v(OUT) v(VDD)
'''


class ClockBuffer(unittest.TestCase):
    def test_total_width_and_fingers_preserved(self):
        definition, devices = buffer(CDL, 'sg13g2_buf_16')
        self.assertEqual(len(devices), 4)
        self.assertEqual(devices[1]['fingers'], 16)
        self.assertAlmostEqual(devices[1]['total_width_um'], 11.84)
        self.assertAlmostEqual(devices[1]['finger_width_um'], .74)
        self.assertEqual(devices[2]['gate'], 'net1')
        self.assertIn('XP1 X net1 VDD VDD', definition)

    def test_only_gate_output_connection_changes(self):
        definition, _ = buffer(CDL, 'sg13g2_buf_16')
        result = connect(DECK, definition, 'sg13g2_buf_16')
        self.assertIn('Cload OUT 0 200f IC=0', result)
        self.assertIn('Xclock_buffer OUT BUFFER_IN VDD 0 sg13g2_buf_16', result)
        self.assertIn('Xgate VDD 0 CLOCK ENABLE GATE_OUT ExtractedAnalog', result)
        self.assertIn('Rclock_link GATE_OUT BUFFER_IN 30', result)
        self.assertIn('.save v(CLOCK) v(ENABLE) v(OUT) v(VDD)', result)
        with self.assertRaisesRegex(ValueError, 'connection'):
            connect(result, definition, 'sg13g2_buf_16')
        with self.assertRaisesRegex(ValueError, 'connection'):
            connect(DECK + DECK, definition, 'sg13g2_buf_16')

    def test_changed_library_rejected(self):
        for bad in (CDL + CDL, CDL.replace('X A VDD VSS', 'A X VDD VSS'),
                    CDL.replace('sg13_lv_nmos', 'unknown'), CDL.replace('ng=16', 'ng=1')):
            with self.assertRaises(ValueError):
                buffer(bad, 'sg13g2_buf_16')


if __name__ == '__main__':
    unittest.main()
