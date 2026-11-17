#!/usr/bin/env python3
"""Regress mixed LV/HV geometry binding against the actual single-slice GDS."""
import argparse,copy,json,re,sys,tempfile,unittest
from pathlib import Path
ROOT=Path(__file__).resolve().parents[4]
sys.path.insert(0,str(ROOT/'analog/1G/IHPSG13G2'))
from mos_junction_extraction import restore
from process import lower,rppd,mim
from layout import validate
from rc_metrics import run as wire_metrics
from reduce_extracted_wires import reduce

class Guards(unittest.TestCase):
    def restore(self,text=None,g=None,lv=True):
        return restore(self.placement/'adc_sar_slice.gds',self.g if g is None else g,self.ext/'named/ExtractedAnalog.res.ext',self.text if text is None else text,include_lv=lv)
    def test_all_fingers_and_diffusions_conserved(self):
        self.assertEqual(self.audit['devices'],sum(p['geometry']['fingers'] for p in self.g['components'] if p['kind']=='mos'))
        self.assertEqual(len(self.audit['components']),59)
        for c in self.audit['components'].values():
            self.assertAlmostEqual(c['gds_area_m2']*1e12,c['after_area_m2']*1e12,places=9)
            self.assertAlmostEqual(c['gds_perimeter_m']*1e6,c['after_perimeter_m']*1e6,places=9)
    def test_documented_3p3_gate_lengths(self):
        for p in self.g['components']:
            if p['kind']!='mos':continue
            g=p['geometry'];minimum={'sg13_hv_nmos':.6,'sg13_hv_pmos':.5}.get(g['model'],.13)
            self.assertGreaterEqual(g['length_um']+1e-12,minimum)
            self.assertLessEqual(g['finger_width_um'],10+1e-12)
    def test_idempotent(self):
        text,audit=self.restore(self.corrected);self.assertEqual(text,self.corrected);self.assertEqual(audit['changes'],[])
    def test_lv_requires_explicit_opt_in(self):
        with self.assertRaisesRegex(ValueError,'unreviewed MOS macro'):self.restore(lv=False)
    def test_missing_duplicate_and_resized_lv_rejected(self):
        line=next(l for l in self.text.splitlines() if ' sg13_lv_pmos ' in l)
        for altered in (self.text.replace(line+'\n','',1),self.text.replace(line,line+'\n'+line,1),self.text.replace(line,re.sub(r'\bl=\S+','l=.51u',line),1)):
            with self.subTest(altered=altered[:40]),self.assertRaises(ValueError):self.restore(altered)
    def test_placement_and_finger_count_guard(self):
        for key in ('origin','fingers'):
            g=copy.deepcopy(self.g);p=next(p for p in g['components'] if p['kind']=='mos' and p['geometry']['model']=='sg13_lv_pmos')
            if key=='origin':p['origin_um'][0]+=.001
            else:p['geometry']['fingers']+=1
            with self.subTest(key=key),self.assertRaises(ValueError):self.restore(g=g)
    def test_no_passive_or_connectivity_change(self):
        for a,b in zip(self.text.splitlines(),self.corrected.splitlines()):
            f=a.split();h=b.split()
            if f and f[0].startswith('X') and any('sg13_' in s for s in f):self.assertEqual(f[:6],h[:6])
            else:self.assertEqual(a,b)
    def test_process_geometry_is_explicit(self):
        bundle=rppd(self.pdk,dict(w=2e-6,l=7.45e-6));self.assertNotIn('m',bundle['instance'])
        self.assertEqual(bundle['instance']['w'],2e-6);self.assertEqual(bundle['instance']['l'],7.45e-6)
        self.assertAlmostEqual(mim(self.pdk,dict(w=7e-6,l=7e-6),27),49*1.5e-15+28*40e-18,delta=1e-24)
    def test_zero_body_removal_and_nonphysical_passives(self):
        with tempfile.TemporaryDirectory() as directory:
            work=Path(directory)
            r=lower('.subckt t a b\nC0 a b 0\nR1 a b 1\n.ends\n',self.pdk,work)
            self.assertEqual(r['counts'],{'zero_capacitors_omitted':1,'R':1})
            for line in ('R0 a b 0','C0 a b -1e-15','R0 a b -1'):
                with self.subTest(line=line),self.assertRaises(ValueError):lower('.subckt t a b\n'+line+'\n.ends\n',self.pdk,work)
    def test_fractional_finger_count_rejected(self):
        with tempfile.TemporaryDirectory() as directory,self.assertRaisesRegex(ValueError,'nonintegral MOS'):
            lower('.subckt t d g s b\nX1 d g s b sg13_hv_nmos w=10u l=.45u ng=1.5\n.ends\n',self.pdk,Path(directory))
    def test_distributed_physical_probe_binding(self):
        # An unresisted .ext alias points at the external port and would
        # incorrectly report zero ohms for every physical terminal.
        with tempfile.TemporaryDirectory() as directory:
            work=Path(directory)
            reduced,_=reduce(self.corrected,self.g['ports'],())
            lower(reduced,self.pdk,work)
            report=wire_metrics(work/'circuit.txt',self.ext/'named/ExtractedAnalog.res.ext',self.placement/'placement.json')
            self.assertEqual(len({(r['port'],r['terminal']) for r in report['measurements']}),10)
            self.assertTrue(all(0<r['resistance_Ohm']<1e6 and r['node']!=r['port'] for r in report['measurements']))
            with self.assertRaises(ValueError):wire_metrics(work/'circuit.txt',self.ext/'named/ExtractedAnalog.ext',self.placement/'placement.json')

if __name__=='__main__':
    p=argparse.ArgumentParser(description=__doc__)
    for name in ('placement','physical','pdk'):p.add_argument('--'+name,type=Path,required=True)
    a=p.parse_args();Guards.placement=a.placement;Guards.ext=a.physical/'extraction';Guards.pdk=a.pdk.resolve();Guards.g=json.loads((a.placement/'placement.json').read_text());Guards.text=(Guards.ext/'conserved.spice').read_text();Guards.corrected,Guards.audit=Guards().restore()
    r=unittest.TextTestRunner(verbosity=2).run(unittest.defaultTestLoader.loadTestsFromTestCase(Guards));sys.exit(not r.wasSuccessful())
