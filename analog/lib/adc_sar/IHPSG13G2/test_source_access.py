"""Check that source reinforcement changes conductors, never device masks."""
import argparse,copy,json,unittest
from pathlib import Path
import klayout.db as db
from source_access import strengthen_sources

HERE=Path(__file__).resolve().parent

class SourceAccess(unittest.TestCase):
    gds=HERE/'adc_sar_slice.gds'
    geometry=HERE/'adc_sar_slice.placement.json'
    def fixture(self):
        layout=db.Layout();layout.read(str(self.gds))
        return layout,layout.top_cell(),json.loads(self.geometry.read_text())

    def test_device_masks_and_connections_are_unchanged(self):
        layout,top,g=self.fixture();before=copy.deepcopy(g)
        # Flatten each protected mask before and after the overlay. This also
        # catches unintended changes inside shared PCell hierarchy. Allow only
        # Metal1--4 and Via1--3; wells, gates, active, contacts and MIMs must match.
        mutable={8,10,30,50,19,29,49}
        protected={i:db.Region(top.begin_shapes_rec(i)) for i in layout.layer_indices()
                   if layout.get_info(i).layer not in mutable}
        result=strengthen_sources(layout,top,g,{'VDD','VSS','VCORE'},widen_bus=True)
        self.assertGreater(len(result),20)
        self.assertEqual(g,before)
        for i,region in protected.items():
            self.assertTrue((region ^ db.Region(top.begin_shapes_rec(i))).is_empty(),str(layout.get_info(i)))
        for entry in result:
            part=next(p for p in g['components'] if p['name']==entry['component'])
            cell=layout.cell(part['name']);sy=part['geometry']['terminals_um']['S'][1]
            cuts=db.Region(cell.begin_shapes_rec(layout.layer(19,0))).merged()
            for y in (sy,sy+.42,sy+.84):
                count=sum(abs(p.bbox().center().y*.001-y)<1e-9 for p in cuts.each())
                self.assertEqual(count,part['geometry']['source_straps'])

    def test_unreviewed_units_and_short_fingers_are_rejected(self):
        layout,top,g=self.fixture();layout.dbu=.002
        with self.assertRaisesRegex(ValueError,'1 nm'):strengthen_sources(layout,top,g,{'VSS'})
        layout.dbu=.001
        part=next(p for p in g['components'] if p['kind']=='mos' and p['connections']['S']=='VSS')
        part['geometry']['finger_width_um']=2
        with self.assertRaisesRegex(ValueError,'strap geometry'):strengthen_sources(layout,top,g,{'VSS'})

if __name__=='__main__':
    p=argparse.ArgumentParser(description=__doc__);p.add_argument('--placement',type=Path)
    a,remaining=p.parse_known_args()
    if a.placement:
        SourceAccess.gds=a.placement/'adc_sar_slice.gds';SourceAccess.geometry=a.placement/'placement.json'
    unittest.main(argv=[__file__,*remaining])
