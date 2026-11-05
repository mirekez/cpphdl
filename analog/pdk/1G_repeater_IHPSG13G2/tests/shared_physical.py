"""Check actual leaf coverage and RTL array-bit ordering before shared PnR."""
import argparse
from collections import defaultdict
import json
import math
from pathlib import Path
import re
import random
import subprocess
import sys
import tempfile
import unittest

HERE=Path(__file__).resolve().parents[1]
sys.path.insert(0,str(HERE))
from shared_floorplan import digital_bit, pack
from shared_analog import bind
from shared_cut_rows import free_spans
from shared_sparse_legalize import FreeRow


class Connections(unittest.TestCase):
    def test_sparse_legalizer_against_site_occupancy(self):
        randomizer = random.Random(130)
        for _ in range(30):
            row = FreeRow([(0, 40), (60, 100)])
            free = set(range(40)) | set(range(60, 100))
            for _ in range(50):
                width, target = randomizer.randrange(1, 12), randomizer.randrange(-10, 110)
                legal = [x for x in range(101 - width) if set(range(x, x + width)) <= free]
                x = row.reserve(width, target)
                if not legal:
                    self.assertIsNone(x)
                else:
                    self.assertIn(x, legal)
                    self.assertEqual(abs(x - target), min(abs(p - target) for p in legal))
                    free.difference_update(range(x, x + width))

    def test_row_subtraction_preserves_legal_sites(self):
        randomizer = random.Random(13)
        for _ in range(100):
            blocked = [sorted((randomizer.randrange(-10, 110), randomizer.randrange(-10, 110)))
                       for _ in range(randomizer.randrange(8))]
            legal = [not any(a <= i < b for a, b in blocked) for i in range(100)]
            expected = []
            start = None
            for i, available in enumerate(legal + [False]):
                if available and start is None:
                    start = i
                if not available and start is not None:
                    if i - start >= 10:
                        expected.extend(range(start, i))
                    start = None
            actual = [i for a, b in free_spans(100, blocked) for i in range(a, b)]
            self.assertEqual(actual, expected)

    def test_sv2v_array_order(self):
        sv2v=HERE.parents[1]/'out/repeater-toolchain/bin/sv2v'
        self.assertTrue(sv2v.is_file())
        for name,count,width in [('trial_out',128,8),('comparisons_in',8,16),('main_p_out',8,4),('main_n_out',8,4),('history_p_out',8,4),('history_n_out',8,4)]:
            with tempfile.TemporaryDirectory() as work:
                source=Path(work)/'order.sv'
                # Each actual RTL array is ascending; packed element bits are
                # descending. Verify the boundary against the real converter.
                source.write_text(f'module Order(input wire [{width-1}:0] {name}[{count}], output wire first, last);\nassign first = {name}[0][0];\nassign last = {name}[{count-1}][{width-1}];\nendmodule\n')
                result=subprocess.check_output([str(sv2v),str(source)],text=True)
                for label,index,bit in [('first',0,0),('last',count-1,width-1)]:
                    wanted=digital_bit(f'{name}[{index}][{bit}]')
                    self.assertRegex(result,rf'assign {label}\s*=\s*{re.escape(wanted)}\s*;')
        self.assertEqual(digital_bit('acquire_out[16]'),'acquire_out[16]')

    def test_context_specific_transistors(self):
        def inverter(owner):
            return [dict(kind=k,owner=owner,file='basic/inverter.hpp',beta=.003,threshold=.7,nodes=[4,1,5]) for k in ('pmos','nmos')]
        data=dict(components=[dict(id=0,parent=None,**{'class':'root'}),dict(id=1,parent=0,**{'class':'differential_receive_gain'}),dict(id=2,parent=1,**{'class':'cmos'}),dict(id=3,parent=0,**{'class':'clock_level_shifter'}),dict(id=4,parent=3,**{'class':'cmos'})],ports=[dict(name=n,node=i) for i,n in enumerate(('VSS','VDD','VLOGIC'))],elements=inverter(2)+inverter(4))
        widths=[d['width_um'] for d in bind(data)['devices']]
        self.assertEqual(widths,[30,10,40,20])
        data['elements'][0]['beta']=.0031
        with self.assertRaises(ValueError):
            bind(data)


def full(plan_path,leaves_path):
    import klayout.db as db
    plan=json.loads(plan_path.read_text())
    leaves=json.loads((leaves_path/'leaves.json').read_text())
    by_source=defaultdict(list)
    for d in leaves['instances']:
        by_source[d['source_id']].append(d)
    assert set(by_source)==set(range(len(plan['devices']))), 'physical inventory lost source elements'
    for source in plan['devices']:
        actual=by_source[source['source_id']]
        if source['kind'] in ('nmos','pmos'):
            assert all(d['connections']==source['connections'] for d in actual)
            assert all(d['length_um']==source['length_um'] and d['domain']==source['domain'] and d['polarity']==source['polarity'] for d in actual)
            assert math.isclose(sum(d['width_um'] for d in actual),source['width_um'],rel_tol=1e-12)
        elif source['kind']=='capacitor':
            assert len(actual)==1 and actual[0]['connections']==source['connections'] and actual[0]['value']==source['value']
        else:
            first,last=source['connections']['PLUS'],source['connections']['MINUS']
            assert all(d['connections']['SUB']==source['connections']['SUB'] for d in actual)
            if all(d['connections']['PLUS']==first and d['connections']['MINUS']==last for d in actual):
                resistance=1/sum(1/d['value'] for d in actual)
            else:
                edges=defaultdict(list)
                for i,d in enumerate(actual):
                    a,b=d['connections']['PLUS'],d['connections']['MINUS']
                    edges[a].append((b,i,d['value']))
                    edges[b].append((a,i,d['value']))
                assert len(edges[first])==len(edges[last])==1
                assert all(len(v)==2 for k,v in edges.items() if k not in (first,last))
                seen=set()
                here=first
                resistance=0
                while here!=last:
                    choices=[e for e in edges[here] if e[1] not in seen]
                    assert len(choices)==1, 'broken resistor series chain'
                    here,i,value=choices[0]
                    seen.add(i)
                    resistance+=value
                assert len(seen)==len(actual), 'disconnected resistor units'
            assert math.isclose(resistance,source['value'],rel_tol=1e-10)
    layout=db.Layout()
    layout.read(str(leaves_path/'analog.gds.gz'))
    pins=0
    for name,info in leaves['masters'].items():
        cell=layout.cell(name)
        assert cell is not None and not cell.is_empty()
        bounds=cell.dbbox()
        assert bounds.left>=0 and bounds.bottom>=0 and bounds.right<=info['width_um']+.001 and bounds.top<=info['height_um']+.001
        for pin,p in info['terminals'].items():
            point=db.Point(round(p['x_um']/layout.dbu),round(p['y_um']/layout.dbu))
            metal=db.Region(cell.begin_shapes_rec(layout.layer(p['layer'],0)))
            assert not metal.interacting(db.Region(db.Box(point.x-1,point.y-1,point.x+1,point.y+1))).is_empty(),(name,pin)
            pins+=1
    print(json.dumps(dict(source_laws=len(by_source),physical_leaves=len(leaves['instances']),masters=len(leaves['masters']),connected_master_pins=pins)))

if __name__=='__main__':
    parser=argparse.ArgumentParser()
    parser.add_argument('--plan',type=Path)
    parser.add_argument('--leaves',type=Path)
    args=parser.parse_args()
    result=unittest.TextTestRunner().run(unittest.defaultTestLoader.loadTestsFromTestCase(Connections))
    if not result.wasSuccessful():
        raise SystemExit(1)
    if args.plan:
        full(args.plan,args.leaves)
