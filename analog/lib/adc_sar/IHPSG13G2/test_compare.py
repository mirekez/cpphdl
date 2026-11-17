"""Checks that grid refinement cannot hide a narrow waveform discrepancy."""
import csv,math,tempfile,unittest
from pathlib import Path
from compare import waveform

class Waveforms(unittest.TestCase):
    def write(self,directory,name,rows):
        path=Path(directory)/name;path.mkdir()
        with (path/'trace.csv').open('w') as f:
            w=csv.writer(f);w.writerow(['time_s','held']);w.writerows(rows)
        return path
    def test_union_finds_reference_only_peak_and_exact_rms(self):
        with tempfile.TemporaryDirectory() as d:
            a=self.write(d,'a',[(0,0),(1,0)])
            b=self.write(d,'b',[(0,0),(.5,1),(1,0)])
            for left,right in [(a,b),(b,a)]:
                r=waveform(left,right)
                self.assertEqual(r['peak_V'],1)
                self.assertEqual(r['nodes']['held']['peak_time_s'],.5)
                self.assertAlmostEqual(r['rms_V'],math.sqrt(1/3))
    def test_identical_linear_trace_on_distinct_grids(self):
        with tempfile.TemporaryDirectory() as d:
            a=self.write(d,'a',[(0,0),(1,2)])
            b=self.write(d,'b',[(0,0),(.25,.5),(.75,1.5),(1,2)])
            self.assertEqual(waveform(a,b)['peak_V'],0)
    def test_truncated_or_invalid_trace_is_rejected(self):
        with tempfile.TemporaryDirectory() as d:
            a=self.write(d,'a',[(0,0),(1,0)])
            for i,rows in enumerate([[(0,0),(.9,0)],[(0,0),(.5,float('nan')),(1,0)],[(0,0),(.5,0),(.5,0),(1,0)]]):
                b=self.write(d,str(i),rows)
                with self.assertRaises(ValueError):waveform(a,b)

if __name__=='__main__':unittest.main()
