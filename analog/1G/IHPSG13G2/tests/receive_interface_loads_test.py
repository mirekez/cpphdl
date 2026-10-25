#!/usr/bin/env python3
"""Guard the load boundary used to connect physical comparator outputs."""
from pathlib import Path
import sys
import unittest

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))
from receive_interface_loads import extract

LIBRARY = '''library(test) {
  capacitive_load_unit (1, pf);
  cell(load) {
    pin(A) {
      direction : "input";
      capacitance : 0.002;
      rise_capacitance_range (0.001, 0.004);
      fall_capacitance : 0.003;
      internal_power () { values ("}"); }
    }
  }
}'''
SPEF = '''*C_UNIT 1 PF
*DESIGN_FLOW "PIN_CAP NONE"
*DELIMITER :
*NAME_MAP
*1 comparisons_in[0]
*2 delay_1
*3 other_net
*D_NET *1 0.015
*CONN
*P comparisons_in[0] I
*I *2:A I *D load
*CAP
1 *2:A 0.010
2 *2:A *3:1 0.005
*RES
1 *1 *2:A 3.2
*END
'''


class Loads(unittest.TestCase):
    def report(self, spef=SPEF, library=LIBRARY):
        return extract(spef, {'slow': library}, count=1)[0]

    def test_separate_pin_wire_and_switching_bounds(self):
        report = self.report()
        self.assertAlmostEqual(report['grounded_wire_pf'], 0.010)
        self.assertAlmostEqual(report['coupled_wire_pf'], 0.005)
        corner = report['corners']['slow']
        self.assertAlmostEqual(corner['grounded_aggressor_total_pf'], 0.017)
        self.assertAlmostEqual(corner['grounded_aggressor_characterized_total_pf'], 0.019)
        self.assertAlmostEqual(corner['opposite_equal_swing_characterized_total_pf'], 0.024)
        self.assertEqual(self.report(SPEF.replace('*P comparisons_in[0]', '*P *1')), report)

    def test_no_silent_missing_or_double_counted_pin_loads(self):
        for old, new in [('"PIN_CAP NONE"', '"PIN_CAP INPUT_OUTPUT"'),
                ('*C_UNIT 1 PF', '*C_UNIT 1 FF'), ('*I *2:A I', '*I *2:A O'),
                ('*I *2:A I *D load\n', ''), ('*I *2:A', '*I *2:B'),
                ('*2 delay_1', '*2 delay_1\n*2 another_cell')]:
            with self.subTest(old=old), self.assertRaises(ValueError):
                self.report(SPEF.replace(old, new))

    def test_incomplete_or_invalid_extraction_rejected(self):
        for old, new in [('0.015', '0.020'), ('0.005', '-0.005'),
                ('0.005', 'nan'), ('comparisons_in[0]', 'comparisons_in[13]'),
                ('2 *2:A *3:1', '1 *2:A *3:1'), ('*P comparisons_in[0] I', '*P other_net I')]:
            with self.subTest(old=old), self.assertRaises(ValueError):
                self.report(SPEF.replace(old, new))

    def test_library_load_must_be_known_and_valid(self):
        for old, new in [('(1, pf)', '(1, ff)'), ('"input"', '"output"'),
                ('capacitance : 0.002;', ''), ('(0.001, 0.004)', '(0.001, nan)'),
                ('cell(load)', 'cell(other)')]:
            with self.subTest(old=old), self.assertRaises(ValueError):
                self.report(library=LIBRARY.replace(old, new))


if __name__ == '__main__':
    unittest.main()
