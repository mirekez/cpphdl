import csv
import math
from pathlib import Path
import sys
import tempfile
import unittest
sys.path.insert(0,str(Path(__file__).resolve().parents[1]))
from noise_calibration import fit_psd,fit_transition_noise,calibrate


class NoiseCalibration(unittest.TestCase):
    def test_known_white_and_flicker_and_held_out_frequencies(self):
        f=[10**(3+i/4) for i in range(17)]
        result=fit_psd(f,[4e-16+4e-10/x for x in f])
        self.assertAlmostEqual(result['white_psd_V2_Hz']/4e-16,1,places=10)
        self.assertAlmostEqual(result['flicker_V2']/4e-10,1,places=10)
        for frequency in (1317,49231,331756,6432154):
            self.assertAlmostEqual((result['white_psd_V2_Hz']+result['flicker_V2']/frequency)/(4e-16+4e-10/frequency),1,places=10)

    def test_white_only_does_not_invent_negative_flicker(self):
        r=fit_psd([1,10,100,1000],[1e-16]*4)
        self.assertGreaterEqual(r['flicker_V2'],0)
        self.assertLess(r['relative_rms_residual'],1e-12)

    def test_bad_psd_rejected(self):
        for f,s in [([1,2,3],[1,2,3]),([1,2,2,4],[1]*4),([1,2,3,4],[1,math.nan,1,1]),
                    ([1,2,3,4],[1,0,1,1]),([0,1,2,3],[1]*4)]:
            with self.assertRaises(ValueError):fit_psd(f,s)

    def test_blind_transition_fit_and_confidence_interval(self):
        center=1.648234; sigma=64e-6
        levels=[center+i*sigma for i in (-2,-1,0,1,2)]
        count=[10000]*5
        high=[round(n*.5*math.erfc(-(v-center)/(sigma*math.sqrt(2)))) for v,n in zip(levels,count)]
        r=fit_transition_noise(levels,high,count)
        self.assertLess(abs(r['sigma_V']/sigma-1),.001)
        self.assertLess(abs(r['center_V']-center),1e-9)
        self.assertLess(r['sigma_95_interval_V'][0],sigma)
        self.assertGreater(r['sigma_95_interval_V'][1],sigma)

    def test_unresolved_code_histogram_does_not_claim_zero_noise(self):
        for h,n in [([0,0,20],[20]*3),([20]*3,[20]*3),([0,4,25],[20]*3)]:
            with self.assertRaises(ValueError):fit_transition_noise([1,2,3],h,n)

    def test_provenance_and_sampling_double_count_guard(self):
        with tempfile.TemporaryDirectory() as d:
            p=Path(d)/'psd.csv'
            with p.open('w',newline='') as out:
                w=csv.writer(out);w.writerow(['frequency_Hz','input_psd_V2_Hz'])
                for f in (1e3,1e4,1e5,1e6,1e7):w.writerow([f,4e-16+4e-10/f])
            for included in (False,True):
                r=calibrate(p,'synthetic','independent analytic fixture','27 C',included,1e3,8e6,300.15)
                self.assertEqual(r['profile']['include_ktc'],not included)
                self.assertEqual(r['source_kind'],'synthetic');self.assertEqual(len(r['input_sha256']),64)
            with self.assertRaises(ValueError):calibrate(p,'measurement','','27 C',False,1e3,8e6,300.15)
            with self.assertRaises(ValueError):calibrate(p,'measurement','test','27 C',False,1,8e6,300.15)
            with p.open('w',newline='') as out:
                w=csv.writer(out);w.writerow(['frequency_Hz','input_psd_V2_Hz'])
                for f,s in zip((1e3,1e4,1e5,1e6,1e7),(1,100,1,100,1)):w.writerow([f,s*1e-16])
            with self.assertRaisesRegex(ValueError,'residual'):calibrate(p,'synthetic','test','27 C',False,1e3,8e6,300.15)


if __name__=='__main__':unittest.main()
