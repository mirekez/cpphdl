#!/usr/bin/env python3
"""Test sampled functional headroom separately from settling excursions."""
import unittest

from differential_receive_conversion import contract

HEADER = ['time_s', 'PLUS', 'MINUS', 'GAIN_INPUT', 'GAIN_OUTPUT', 'VDD', 'VLOGIC', 'GAIN_BIAS',
          'MDI_P', 'MDI_N', 'VREF', 'HELD0', 'TRACK0', 'CORE_DECISION0']


class SampledHeadroom(unittest.TestCase):
    @staticmethod
    def case():
        metadata = dict(slices=1, launches=1, clocks=25, period_s=4e-9, clock_origin_s=16e-6,
            precharge=True, normalizer_offset=112)
        controls = [dict(clock=i, fault=0, valid=int(i == 13), data=112, slice=0,
            normalized_valid=int(i == 21), normalized_slice=0, normalized=128, clipped=0,
            acquire=int(i in (-1, 1, 2)), convert=int(4 <= i <= 11)) for i in range(-1, 25)]
        rows = [[16e-6 + i * .1e-9, 1.9053125, 1.8946875, 1.25, 1.45, 3.3, 1.2, 1.3,
                 2.55, 2.05, 1.45, 1.45, 3.3 if i < 20 else 0, 1.2] for i in range(1001)]
        # A settling undershoot, well before the 14 ns capture aperture.
        rows[40][4] = .7
        return rows, controls, metadata

    def evaluate(self, rows, controls, metadata):
        return contract(HEADER, rows, controls, metadata)

    def test_settling_excursion_does_not_redefine_captured_input(self):
        result = self.evaluate(*self.case())
        self.assertTrue(all(result['checks'].values()), result['checks'])
        self.assertEqual(result['gain_output_range_V'], [.7, 1.45])
        self.assertEqual(result['gain_output_outside_conversion_range_states'], 1)
        self.assertEqual(result['held_ranges_V'], [[1.45, 1.45]])

    def test_captured_input_must_remain_in_functional_range(self):
        rows, controls, metadata = self.case()
        for row in rows:
            if abs(row[0] - (16e-6 + 14e-9)) <= .11e-9:
                row[4] = .8
        result = self.evaluate(rows, controls, metadata)
        self.assertFalse(result['checks']['captured_headroom'])
        self.assertFalse(all(result['checks'].values()))

    def test_every_held_state_is_checked(self):
        rows, controls, metadata = self.case()
        rows[300][11] = .84
        result = self.evaluate(rows, controls, metadata)
        self.assertFalse(result['checks']['held_headroom'])

    def test_held_window_includes_the_last_decision(self):
        rows, controls, metadata = self.case()
        rows[500][11] = .84
        self.assertFalse(self.evaluate(rows, controls, metadata)['checks']['held_headroom'])

    def test_rail_excursion_is_rejected_even_between_apertures(self):
        for value in (-.01, 3.31):
            rows, controls, metadata = self.case()
            rows[40][4] = value
            result = self.evaluate(rows, controls, metadata)
            self.assertFalse(result['checks']['terminal_range'])

    def test_precharge_requires_real_held_charge_and_tracking(self):
        for column, value in ((11, 0), (12, 0)):
            rows, controls, metadata = self.case()
            rows[0][column] = value
            result = self.evaluate(rows, controls, metadata)
            self.assertFalse(result['checks']['physical_precharge'])
        rows, controls, metadata = self.case()
        controls[0]['acquire'] = 0
        self.assertFalse(self.evaluate(rows, controls, metadata)['checks']['physical_precharge'])

    def test_original_accuracy_and_hold_limits_remain(self):
        rows, controls, metadata = self.case()
        controls[14]['data'] = 116
        self.assertFalse(self.evaluate(rows, controls, metadata)['checks']['conversion_accuracy'])
        rows, controls, metadata = self.case()
        rows[300][11] += .021
        self.assertFalse(self.evaluate(rows, controls, metadata)['checks']['hold'])


if __name__ == '__main__':
    unittest.main()
