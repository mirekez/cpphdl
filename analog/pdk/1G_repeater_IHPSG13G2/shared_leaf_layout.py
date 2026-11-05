"""Generate reusable PCell GDS/LEF leaves for the complete shared inventory."""
import argparse
from collections import Counter
import hashlib
import json
import math
from pathlib import Path
import sys

from shared_analog import ANALOG
import klayout.db as db
from mos_layout import append_mos
from capacitor_layout import append_mim
from capacitor_series_layout import append_mim_series
from capacitor_bank_layout import append_mim_bank
from oscillator_passive_layout import append_one_ff, append_rhigh_unit
from resistor_layout import append_rppd

METALS = {8:'Metal1', 10:'Metal2', 30:'Metal3', 50:'Metal4', 67:'Metal5', 126:'TopMetal1', 134:'TopMetal2'}


def expand(device):
    """Preserve explicit large R with real series units, small R in parallel."""
    if device['kind'] in ('nmos','pmos') and device['width_um'] > 500:
        count = next(n for n in range(math.ceil(device['width_um']/500),device['fingers']+1)
                     if device['fingers'] % n == 0)
        return [dict(device, name=device['name']+f'_m{i}', width_um=device['width_um']/count,
                     fingers=device['fingers']//count) for i in range(count)]
    if device['kind'] != 'resistor':
        return [device]
    value = device['value']
    series = max(1, math.ceil(value / 200000))
    parallel = max(1, math.ceil(400 / value)) if value < 100 else 1
    parts = []
    for index in range(series * parallel):
        d = dict(device)
        d['name'] += f'_r{index}'
        d['value'] = value / series * parallel
        d['connections'] = dict(device['connections'])
        if series > 1:
            if index:
                d['connections']['PLUS'] = f"{device['name']}_junction{index}"
            if index + 1 < series:
                d['connections']['MINUS'] = f"{device['name']}_junction{index+1}"
        d['resistor_width_um'] = 12.5 if parallel > 1 else 1 if value == 12150 else 2
        parts.append(d)
    return parts


def key(d):
    if d['kind'] in ('nmos','pmos'):
        return (d['kind'],d['width_um'],d['length_um'],d['fingers'],d['domain'])
    return (d['kind'],d['value'],d.get('resistor_width_um',0))


def run(args):
    plan = json.loads(args.plan.read_text())
    args.output.mkdir(parents=True, exist_ok=True)
    layout = db.Layout()
    layout.dbu = .001
    instances = [p for d in plan['devices'] for p in expand(d)]
    masters, keys = {}, {}
    lef = ['VERSION 5.8 ;', 'BUSBITCHARS "[]" ;', 'DIVIDERCHAR "/" ;']
    for d in instances:
        geometry = key(d)
        if geometry in keys:
            d['master'] = keys[geometry]
            continue
        name = 'analog_leaf_' + hashlib.sha256(repr(geometry).encode()).hexdigest()[:16]
        keys[geometry] = name
        d['master'] = name
        core_name = name + '_physical'
        print('building', geometry, flush=True)
        if d['kind'] in ('nmos','pmos'):
            core, info = append_mos(layout,args.pdk,core_name,d['width_um'],d['length_um'],d['fingers'],d['polarity'],d['domain'])
            pins = {n:dict(x_um=p[0],y_um=p[1],layer=10) for n,p in info['terminals_um'].items()}
        elif d['kind'] == 'capacitor':
            v=d['value']
            builder = append_one_ff if math.isclose(v,1e-15,rel_tol=1e-12,abs_tol=0) else append_mim_series if v < 3e-15 else append_mim_bank if v > 5e-12 else append_mim
            core,info=builder(layout,args.pdk,core_name,v)
            pins=info['terminals']
        elif d['value'] >= 100000:
            core,info=append_rhigh_unit(layout,args.pdk,core_name,d['value'])
            pins=info['terminals']
        else:
            # PCell string coercion can round certain lengths. Select an
            # explicit alternative width only if independent geometry checks
            # and the original 1% nominal resistance bound both pass.
            errors=[]
            for width in dict.fromkeys((d['resistor_width_um'],1,.5,4,3)):
                trial=db.Layout()
                trial.dbu=.001
                try:
                    probe,info=append_rppd(trial,args.pdk,core_name,d['value'],width)
                except ValueError as error:
                    errors.append(str(error))
                    continue
                core=layout.create_cell(core_name)
                core.copy_tree(probe)
                break
            else:
                raise ValueError(f'cannot realize resistor {geometry}: {errors}')
            pins=info['terminals']
        box=core.bbox()
        # Route halo belongs to each physical leaf, not a logical module box.
        dx,dy=2000-box.left,2000-box.bottom
        w=math.ceil((box.width()*.001+4)/.48)*.48
        h=math.ceil((box.height()*.001+4)/3.78)*3.78
        cell=layout.create_cell(name)
        cell.insert(db.CellInstArray(core.cell_index(),db.Trans(dx,dy)))
        translated={n:dict(layer=p['layer'],x_um=p['x_um']+dx*.001,y_um=p['y_um']+dy*.001) for n,p in pins.items()}
        masters[name]=dict(width_um=w,height_um=h,terminals=translated,geometry=list(geometry),physical=info)
        lef += [f'MACRO {name}', '  CLASS BLOCK ;', '  ORIGIN 0 0 ;', f'  SIZE {w:.3f} BY {h:.3f} ;', '  SYMMETRY X Y ;']
        openings={layer:db.Region() for layer in METALS}
        for pin,p in translated.items():
            x,y,layer=p['x_um'],p['y_um'],p['layer']
            # Every abstract pin sits on real conductive landing metal.
            half=.82 if layer==126 else .16
            landing=db.Region(db.Box(round((x-half)*1000),round((y-half)*1000),round((x+half)*1000),round((y+half)*1000)))
            if not (landing-db.Region(cell.begin_shapes_rec(layout.layer(layer,0)))).is_empty():
                raise ValueError(f'abstract pin escapes physical conductor: {name}.{pin}')
            lef += [f'  PIN {pin}', '    DIRECTION INOUT ;', '    USE SIGNAL ;', '    PORT', f'      LAYER {METALS[layer]} ;', f'      RECT {x-half:.3f} {y-half:.3f} {x+half:.3f} {y+half:.3f} ;','    END',f'  END {pin}']
            openings[layer].insert(db.Box(round((x-half-.7)*1000),round((y-half-.7)*1000),round((x+half+.7)*1000),round((y+half+.7)*1000)))
        lef.append('  OBS')
        # Retain actual conductive obstructions with pin-access windows. This
        # permits routes on unused upper layers over small analog leaf cells.
        for layer,lname in METALS.items():
            region=db.Region(cell.begin_shapes_rec(layout.layer(layer,0))) - openings[layer]
            if region.is_empty():
                continue
            lef.append(f'    LAYER {lname} ;')
            for polygon in region.merged().each():
                for rectangle in polygon.decompose_trapezoids():
                    b=rectangle.bbox()
                    lef.append('    RECT '+' '.join(f'{v*.001:.3f}' for v in (b.left,b.bottom,b.right,b.top))+' ;')
        lef += ['  END',f'END {name}']
        print(f'master {len(masters)} {geometry} {w:.2f} x {h:.2f} um',flush=True)
    lef.append('END LIBRARY')
    (args.output/'analog.lef').write_text('\n'.join(lef)+'\n')
    layout.write(str(args.output/'analog.gds.gz'))
    report=dict(status='physical_leaves_unplaced',full_chip_qualified=False,masters=masters,instances=instances,
                source_laws=len(plan['devices']),physical_instances=len(instances),unique_masters=len(masters),
                area_um2=sum(masters[d['master']]['width_um']*masters[d['master']]['height_um'] for d in instances),
                plan_sha256=hashlib.sha256(args.plan.read_bytes()).hexdigest(),candidate_flp_owners=plan['candidate_flp_owners'])
    (args.output/'leaves.json').write_text(json.dumps(report,indent=2)+'\n')
    print(json.dumps({k:report[k] for k in ('status','source_laws','physical_instances','unique_masters','area_um2')}))

if __name__=='__main__':
    parser=argparse.ArgumentParser(description=__doc__)
    for n in ('plan','pdk','output'):
        parser.add_argument('--'+n,type=Path,required=True)
    run(parser.parse_args())
