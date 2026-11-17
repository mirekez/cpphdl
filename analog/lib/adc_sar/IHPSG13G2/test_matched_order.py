"""Keep physical pairing constraints intact through placement optimization."""
import copy,json,re,unittest
from pathlib import Path
from types import SimpleNamespace
from matched_order import order,PAIRS

class Placement(unittest.TestCase):
    def parts(self):
        g=json.loads(Path(__file__).with_name('adc_sar_slice.placement.json').read_text())
        lef=Path(__file__).with_name('adc_sar_slice.cells.lef').read_text()
        widths={n:float(w) for n,w in re.findall(r'MACRO (\S+).*?SIZE ([0-9.]+) BY',lef,re.S)}
        parts=[]
        for p in g['components']:
            width=widths[p['name']]
            cell=SimpleNamespace(name=p['name'],bbox=lambda w=width:SimpleNamespace(width=lambda:w*1000))
            parts.append((cell,p['geometry'],p['connections'],p['kind']))
        return parts,g['observations']
    def test_pairs_inventory_and_electrical_connections_survive_optimization(self):
        parts,obs=self.parts();before=copy.deepcopy([p[2] for p in parts])
        weights={obs['held']:10,obs['dac']:10,'IN':12}
        a,meta=order(parts,.001,weights,[obs['positive'],obs['negative']],1000)
        b,_=order(parts,.001,weights,[obs['positive'],obs['negative']],1000)
        self.assertEqual(a,b);self.assertEqual(sorted(a),list(range(len(parts))))
        positions={parts[i][0].name:k for k,i in enumerate(a)}
        for left,right in PAIRS:self.assertEqual(abs(positions['D'+str(left)]-positions['D'+str(right)]),1)
        self.assertEqual(before,[p[2] for p in parts]);self.assertEqual(len(meta['paired_components']),32)
    def test_changed_inventory_and_missing_output_are_rejected(self):
        parts,obs=self.parts()
        with self.assertRaisesRegex(ValueError,'inventory changed'):order([p for p in parts if p[0].name!='D106'],.001,{},[obs['positive'],obs['negative']],1)
        with self.assertRaisesRegex(ValueError,'missing comparator output'):order(parts,.001,{},['absent',obs['negative']],1)
    def test_fixed_order_preserves_pairs_during_resizing(self):
        parts,obs=self.parts();nets=[obs['positive'],obs['negative']]
        chosen,meta=order(parts,.001,{},nets,100)
        fixed=meta['component_order'];parts[0][0].bbox=lambda:SimpleNamespace(width=lambda:100000)
        result,metadata=order(parts,.001,{},nets,fixed_order=fixed)
        self.assertEqual(result,chosen);self.assertTrue(metadata['fixed_order'])
        with self.assertRaisesRegex(ValueError,'every component'):order(parts,.001,{},nets,fixed_order=fixed[:-1])
        broken=fixed[:];a=broken.index('D0');b=broken.index('D64');broken[a],broken[b]=broken[b],broken[a]
        with self.assertRaisesRegex(ValueError,'matched pair'):order(parts,.001,{},nets,fixed_order=broken)

if __name__=='__main__':unittest.main()
