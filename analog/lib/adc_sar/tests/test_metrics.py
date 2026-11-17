import math
import sys
import unittest
from pathlib import Path
sys.path.insert(0, str(Path(__file__).resolve().parents[1]))
from metrics import spectrum, static_linearity, wilson, transition_linearity


class Measurements(unittest.TestCase):
    def test_partial_range_transition_gain_offset(self):
        codes=list(range(66,72)); step=3.3/256
        values=[.001+k*step*1.01 for k in codes]
        r=transition_linearity(codes,values,values,step)
        self.assertEqual(r['complete_codes'],list(range(66,71)))
        self.assertAlmostEqual(r['fitted_offset_V'],.001)
        self.assertAlmostEqual(r['fitted_gain_error_fraction'],.01)
        self.assertLess(r['maximum_abs_inl_lsb'],1e-12)
        self.assertLess(r['maximum_abs_dnl_lsb'],1e-12)
        for d in r['dnl_nominal_lsb']: self.assertAlmostEqual(d,.01)

    def test_transition_bracket_uncertainty_contains_truth(self):
        codes=[10,11,12,13]; volts=[0,1,2.5,4]
        r=transition_linearity(codes,[v-.001 for v in volts],[v+.001 for v in volts],1)
        for observed,expected in zip(r['dnl_lsb'],[-.25,.125,.125]): self.assertAlmostEqual(observed,expected)
        for observed,expected in zip(r['inl_lsb'],[0,-.25,-.125,0]): self.assertAlmostEqual(observed,expected)
        for bounds,value in zip(r['dnl_bounds_lsb'],r['dnl_lsb']): self.assertLessEqual(bounds[0],value); self.assertGreaterEqual(bounds[1],value)
        for bounds,value in zip(r['inl_bounds_lsb'],r['inl_lsb']): self.assertLessEqual(bounds[0],value); self.assertGreaterEqual(bounds[1],value)

    def test_transition_missing_codes_and_invalid_evidence_rejected(self):
        for codes,lo,hi,lsb in [([1,2,3],[1,2,2],[1,2,2],1),
                ([1,3,4],[1,2,3],[1,2,3],1),([1,2,3],[1,2,3],[1,1.9,3],1),
                ([1,2,3],[1,2,math.nan],[1,2,3],1),([1,2,3],[1,2,3],[1,2,3],0),
                ([1,2,3],[1,1.5,3],[1.6,2,3],1)]:
            with self.assertRaises(ValueError): transition_linearity(codes,lo,hi,lsb)

    def test_independent_spectral_amplitudes(self):
        n = 128
        # Orthogonal coherent bins: 0.1 second harmonic and 0.01 unrelated tone.
        values = [2 + math.sin(2*math.pi*7*i/n) + .1*math.sin(2*math.pi*14*i/n)
                  + .01*math.sin(2*math.pi*3*i/n) for i in range(n)]
        result = spectrum(values, 7)
        self.assertAlmostEqual(result['sfdr_db'], 20, places=8)
        self.assertAlmostEqual(result['snr_db'], 40, places=8)
        self.assertAlmostEqual(result['thd_db'], -20, places=8)
        self.assertAlmostEqual(result['sndr_db'], -10*math.log10(.0101), places=8)

    def test_transition_errors_and_missing_codes(self):
        result = static_linearity([0, 1, 2.5, 2.5, 4])
        self.assertEqual(result['dnl_lsb'], [0, .5, -1, .5])
        self.assertEqual(result['inl_lsb'], [0, 0, .5, -.5, 0])
        self.assertEqual(result['missing_codes'], 1)

    def test_yield_cannot_be_100_percent_confident(self):
        lower, upper = wilson(100, 100)
        self.assertAlmostEqual(lower, .9630065, places=6)
        self.assertEqual(upper, 1)
        with self.assertRaises(ValueError):
            wilson(0, 0)

    def test_invalid_records(self):
        for values, bin_index in [([0]*64, 3), ([math.nan]*64, 3), ([1]*4, 1)]:
            with self.assertRaises(ValueError):
                spectrum(values, bin_index)
        with self.assertRaises(ValueError):
            static_linearity([0, 2, 1])


if __name__ == '__main__':
    unittest.main()
