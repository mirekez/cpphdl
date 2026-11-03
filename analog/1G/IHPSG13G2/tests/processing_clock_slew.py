"""A valid pulse count and delay must not hide an invalid receiver input slew."""
from pathlib import Path
import sys
import unittest

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))
from processing_clock_slew import clock_limit, loaded_deck, measure, thresholds
from processing_clock_gate import schedule
from processing_clock_gate_pex_guards import terminals

LIBRARY = '''slew_derate_from_library : 1;
slew_lower_threshold_pct_rise : 20;
slew_upper_threshold_pct_rise : 80;
slew_lower_threshold_pct_fall : 20;
slew_upper_threshold_pct_fall : 80;
'''
SDC = 'set_input_transition 0.1000 [get_ports {clk}]\n'


def ramped(width):
    rows = terminals()
    for row in rows:
        for rise, fall in schedule():
            if rise - width / 2 <= row[0] <= rise + width / 2:
                row[3] = 1.2 * (row[0] - rise + width / 2) / width
            if fall - width / 2 <= row[0] <= fall + width / 2:
                row[3] = 1.2 * (fall + width / 2 - row[0]) / width
    return rows


class ClockSlew(unittest.TestCase):
    def test_library_thresholds_and_receiver_constraint(self):
        result = measure(ramped(100e-12), 1.2, thresholds(LIBRARY), clock_limit(SDC))
        self.assertTrue(result['passes'])
        self.assertAlmostEqual(result['rise_maximum_s'] / 1e-12, 60, places=5)
        self.assertAlmostEqual(result['fall_maximum_s'] / 1e-12, 60, places=5)

    def test_pulses_can_pass_while_slew_fails(self):
        result = measure(ramped(250e-12), 1.2, thresholds(LIBRARY), clock_limit(SDC))
        self.assertEqual(result['pulse_metrics']['pulses'], 32)
        self.assertFalse(result['passes'])
        self.assertAlmostEqual(result['rise_maximum_s'] / 1e-12, 150, places=5)

    def test_truncated_or_recrossing_waveforms_rejected(self):
        rows = ramped(100e-12)
        with self.assertRaisesRegex(ValueError, 'incomplete'):
            measure(rows[:-10], 1.2, thresholds(LIBRARY), 100e-12)
        rise = schedule()[0][0]
        for row in rows:
            if rise + .15e-9 < row[0] < rise + .20e-9:
                row[3] = .7 * 1.2
        with self.assertRaisesRegex(ValueError, 'repeated clock slew'):
            measure(rows, 1.2, thresholds(LIBRARY), 100e-12)

    def test_missing_ambiguous_or_nonfinite_contract_rejected(self):
        for text in ('', SDC + SDC, SDC.replace('0.1000', 'nan'), SDC.replace('0.1000', '-1')):
            with self.subTest(text=text), self.assertRaises(ValueError):
                clock_limit(text)
        for text in ('', LIBRARY + LIBRARY, LIBRARY.replace(': 20', ': 90'),
                     LIBRARY.replace(': 20', ': nan'), LIBRARY.replace('library : 1', 'library : 2')):
            with self.subTest(text=text), self.assertRaises(ValueError):
                thresholds(text)

    def test_only_explicit_load_changes_in_repeated_circuit(self):
        deck = 'physical circuit\nCload OUT 0 200f IC=0\nXgate VDD 0 CLOCK ENABLE OUT ExtractedAnalog\n'
        changed = loaded_deck(deck, 200e-15, 100)
        self.assertEqual(changed, deck.replace('200f', '100f'))
        for value in (0, -1, float('nan'), float('inf'), 201):
            with self.subTest(value=value), self.assertRaises(ValueError):
                loaded_deck(deck, 200e-15, value)
        with self.assertRaisesRegex(ValueError, 'differs from its receipt'):
            loaded_deck(deck, 100e-15, 100)


if __name__ == '__main__':
    unittest.main()
