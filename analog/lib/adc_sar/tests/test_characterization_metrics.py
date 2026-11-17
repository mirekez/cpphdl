import math
from pathlib import Path
import sys
import tempfile
import unittest
sys.path.insert(0,str(Path(__file__).resolve().parent))
from characterization import sinusoid_fit
from waveforms import compare,rail_integrals

class CharacterizationMethods(unittest.TestCase):
    def test_irregular_aperture_sinusoid(self):
        times=[(i+.07*math.sin(i*2.31))*50e-9 for i in range(64)]
        freq=1.5625e6;phase=.37;amplitude=.24;dc=1.45
        values=[dc+amplitude*math.sin(2*math.pi*freq*t+phase) for t in times]
        result=sinusoid_fit(times,values,freq)
        self.assertAlmostEqual(result['dc'],dc,places=12)
        self.assertAlmostEqual(result['amplitude'],amplitude,places=12)
        self.assertAlmostEqual(result['phase_rad'],phase,places=12)
        self.assertLess(result['residual_rms'],1e-12)
    def test_singular_or_nonfinite_rejected(self):
        with self.assertRaises(ValueError):sinusoid_fit([0]*4,[1]*4,1)
        with self.assertRaises(ValueError):sinusoid_fit([0,1,2,3],[1,math.nan,2,3],1)
    def test_known_source_energy_and_charge(self):
        # Constant 2 A IO, 0.1 A core on a deliberately nonuniform grid.
        t=[0,1e-12,3e-12,10e-12]
        result=rail_integrals(dict(time_s=t,source=[3.3]*4,vdd=[2.8]*4,logic_source=[1.2]*4,core=[.2]*4))
        self.assertAlmostEqual(result['io']['energy_J']/66e-12,1)
        self.assertAlmostEqual(result['io']['charge_C']/20e-12,1)
        self.assertAlmostEqual(result['core']['energy_J']/1.2e-12,1)
        self.assertEqual(result['io']['peak_A'],2)
    def test_waveform_shift_and_missing_edges_fail(self):
        with tempfile.TemporaryDirectory() as d:
            import csv
            def write(name,delay=0,amplitude=1):
                p=Path(d)/name
                with p.open('w',newline='') as f:
                    w=csv.writer(f);w.writerow(['time_s','node1','held','source','vdd','logic_source','core'])
                    for i in range(101):
                        t=i*1e-12;v=amplitude*max(0,min(1,(t-30e-12-delay)/10e-12))
                        w.writerow([t,v,.5,3.3,3.2,1.2,1.19])
                return p
            ref=write('ref.csv');same=compare(ref,ref)
            self.assertFalse(same['failed_gates']);self.assertEqual(same['matched_edges'],1)
            shifted=compare(write('shift.csv',delay=5e-12),ref)
            self.assertIn('maximum_edge_difference_s',shifted['failed_gates'])
            self.assertIn('maximum_voltage_difference_V',shifted['failed_gates'])
            missing=compare(write('missing.csv',amplitude=0),ref)
            self.assertIn('edge_count',missing['failed_gates'])
    def test_incomplete_waveform_rejected(self):
        with tempfile.TemporaryDirectory() as d:
            a=Path(d)/'a.csv';b=Path(d)/'b.csv'
            header='time_s,node1,held,source,vdd,logic_source,core\n'
            a.write_text(header+'0,0,0,1,1,1,1\n1,1,1,1,1,1,1\n')
            b.write_text(header+'.1,0,0,1,1,1,1\n1,1,1,1,1,1,1\n')
            with self.assertRaises(ValueError):compare(a,b)

if __name__=='__main__':unittest.main()
