#!/usr/bin/env python3
"""Physical IHP PCells and connected metal, driven by the C++ slice export."""
import argparse
from collections import Counter,defaultdict
import json,math,sys
from pathlib import Path
ROOT=Path(__file__).resolve().parents[4]
sys.path[:0]=[str(ROOT/'analog/1G/IHPSG13G2')]
import klayout.db as db
from mos_layout import append_mos
from capacitor_layout import append_mim
from capacitor_series_layout import append_mim_series
from oscillator_passive_layout import append_one_ff
from resistor_layout import append_rppd
from reference_clock_row import create_row
from power import add_power
from input_route import strengthen_input
from matched_order import order as matched_order
from source_access import strengthen_sources
from track_route import strengthen_track
from parallel_capacitor_layout import append_mim_parallel

LAYERS={8:'Metal1',10:'Metal2',30:'Metal3',50:'Metal4',67:'Metal5',126:'TopMetal1',134:'TopMetal2'}

def validate(circuit):
    if circuit['schema']!=1 or circuit['bits']!=8 or circuit['top']!='adc_sar_slice':raise ValueError('unsupported slice contract')
    ports=circuit['ports'];devices=circuit['devices']
    expected={'IN','TRACK','EVALUATE','CLEAR','DECISION','VDD','VCORE','VSS'}|{f'BIT_N{i}' for i in range(8)}
    if set(ports)!=expected or len(ports)!=len(expected):raise ValueError('physical port mismatch')
    if len({d['name'] for d in devices})!=len(devices):raise ValueError('duplicate physical element')
    connected={n for d in devices for n in d['connections'].values()}
    if not expected<=connected:raise ValueError('unconnected physical port')
    if Counter(d['kind'] for d in devices)['mos']!=59:raise ValueError('changed 8-bit MOS inventory')
    for d in devices:
        if d['kind']!='mos':continue
        if d['model'] in ('sg13_hv_nmos','sg13_hv_pmos'):
            minimum=.6 if d['model']=='sg13_hv_nmos' else .5
            if d['length_um']<minimum-1e-9:raise ValueError('HV gate is too short for the documented 3.3 V condition')
        if not .3-1e-9<=d['width_um']/d['fingers']<=10+1e-9:raise ValueError('MOS finger outside documented model-width range')
    return circuit

def physical_reference(geometry,ports):
    lines=['* Geometry-matched PSP/R3/MIM schematic, no extracted routing.', '.subckt adc_sar_slice '+' '.join(ports)]
    for part in geometry['components']:
        g=part['geometry'];pins=part['connections'];name=part['name']
        if part['kind']=='mos':
            lines.append('X'+name+' '+' '.join(pins[k] for k in ('D','G','S','B'))+f' {g["model"]} w={g["total_width_um"]:.12g}u l={g["length_um"]:.12g}u ng={g["fingers"]}')
        elif part['kind']=='resistor':
            lines.append('X'+name+' '+' '.join(pins[k] for k in ('PLUS','MINUS','SUB'))+f' rppd w={g["width_um"]:.12g}u l={g["length_um"]:.12g}u')
        elif part['kind']=='capacitor':
            count=g.get('capacitors',1);unit=g.get('unit',g)
            if g.get('configuration')=='parallel':
                if count!=2:raise ValueError('unsupported parallel MIM inventory')
                for i in range(count):
                    lines.append(f'X{name}_{i} {pins["PLUS"]} {pins["MINUS"]} cap_cmim w={unit["width_um"]:.12g}u l={unit["length_um"]:.12g}u ic=0')
            else:
                nets=[pins['PLUS']]+[name+'_mid'+str(i) for i in range(count-1)]+[pins['MINUS']]
                for i in range(count):lines.append(f'X{name}_{i} {nets[i]} {nets[i+1]} cap_cmim w={unit["width_um"]:.12g}u l={unit["length_um"]:.12g}u ic=0')
        else:raise ValueError('unknown layout primitive')
    return '\n'.join(lines+['.ends adc_sar_slice',''])

def placement_views(layout,top,geometry,ports,work):
    """DEF is placement/connectivity; actual routed polygons stay in GDS.

    Each LEF pin is taken from its physical PCell landing. Macro bounding boxes
    are normalized for DEF, without moving the corresponding GDS geometry.
    """
    lef=['VERSION 5.8 ;','BUSBITCHARS "[]" ;','DIVIDERCHAR "/" ;','UNITS',' DATABASE MICRONS 1000 ;','END UNITS']
    nets=defaultdict(list);components=[]
    for part in geometry['components']:
        name=part['name'];cell=layout.cell(name);box=cell.bbox();x0,y0=box.left*.001,box.bottom*.001
        g=part['geometry'];pins=g.get('terminals',{})
        if part['kind']=='mos':pins={k:dict(layer=10,x_um=v[0],y_um=v[1]) for k,v in g['terminals_um'].items()}
        lef += [f'MACRO {name}',' CLASS BLOCK ;',' ORIGIN 0 0 ;',f' SIZE {box.width()*.001:.6f} BY {box.height()*.001:.6f} ;',' SYMMETRY X Y ;']
        for pin,net in part['connections'].items():
            p=pins[pin];x,y=p['x_um']-x0,p['y_um']-y0
            lef += [f' PIN {pin}','  DIRECTION INOUT ;','  USE SIGNAL ;','  PORT',f'   LAYER {LAYERS[p["layer"]]} ;',f'   RECT {x-.16:.6f} {y-.16:.6f} {x+.16:.6f} {y+.16:.6f} ;','  END',f' END {pin}']
            nets[net].append(f'( {name} {pin} )')
        lef += [' OBS','  LAYER Metal1 ;',f'  RECT 0 0 {box.width()*.001:.6f} {box.height()*.001:.6f} ;',' END',f'END {name}']
        x,y=part['origin_um'];components.append(f'- {name} {name} + FIXED ( {round((x+x0)*1000)} {round((y+y0)*1000)} ) N ;')
    lef+=['END LIBRARY'];(work/'adc_sar_slice.cells.lef').write_text('\n'.join(lef)+'\n')
    bounds=top.bbox();die=(bounds.right+20000,bounds.top+20000)
    lines=['VERSION 5.8 ;','DIVIDERCHAR "/" ;','BUSBITCHARS "[]" ;','DESIGN adc_sar_slice ;','UNITS DISTANCE MICRONS 1000 ;',f'DIEAREA ( 0 0 ) ( {die[0]} {die[1]} ) ;',f'COMPONENTS {len(components)} ;',*components,'END COMPONENTS',f'PINS {len(ports)} ;']
    for pin in ports:
        p=geometry['terminals'][pin];use='GROUND' if pin=='VSS' else 'POWER' if pin in ('VDD','VCORE') else 'SIGNAL'
        direction='OUTPUT' if pin=='DECISION' else 'INOUT' if use!='SIGNAL' else 'INPUT'
        lines.append(f'- {pin} + NET {pin} + DIRECTION {direction} + USE {use} + LAYER {LAYERS[p["layer"]]} ( -160 -160 ) ( 160 160 ) + FIXED ( {round(p["x_um"]*1000)} {round(p["y_um"]*1000)} ) N ;')
        nets[pin].append(f'( PIN {pin} )')
    lines+=['END PINS',f'NETS {len(nets)} ;']
    lines += ['- '+net+' '+' '.join(pins)+' ;' for net,pins in sorted(nets.items())]
    lines+=['END NETS','END DESIGN'];(work/'adc_sar_slice.def').write_text('\n'.join(lines)+'\n')
    return dict(components=len(components),nets=len(nets),pins=len(ports),die_um=[v*.001 for v in die],scope='fixed PCell placement and connectivity; GDS contains physical routing, DEF has no invented routed wires')

def build(circuit,pdk,work,fixed_order=None,track_overlay=False):
    validate(circuit);work.mkdir(parents=True,exist_ok=True)
    def parts(layout):
        result=[]
        for d in circuit['devices']:
            name=d['name'];kind=d['kind']
            if kind=='mos':
                _,domain,polarity=d['model'].split('_');cell,info=append_mos(layout,pdk,name,round(d['width_um'],9),round(d['length_um'],9),d['fingers'],polarity[0],domain)
            elif kind=='resistor':
                cell,info=append_rppd(layout,pdk,name,d['value'],width_um=10 if d['value']<200 else 4 if d['value']<500 else 2)
            elif kind=='capacitor':
                value=d['value']
                function=(append_one_ff if value<1.5e-15 else
                          append_mim_series if value<3e-15 else
                          append_mim_parallel if value>8e-12 else append_mim)
                cell,info=function(layout,pdk,name,value)
            else:raise ValueError('unsupported physical contribution')
            info['cpphdl_element']=d['id'];result.append((cell,info,d['connections'],kind))
        return result
    weights={circuit['observations'][k]:10 for k in ('held','dac')};weights.update(IN=12,TRACK=8,EVALUATE=8,VCORE=.01)
    order=lambda parts,dbu:matched_order(parts,dbu,weights,[circuit['observations'][k] for k in ('positive','negative')],fixed_order=fixed_order)
    layout,top,g=create_row('adc_sar_slice',circuit['ports'],parts,order_parts=order,
        priority_nets=[circuit['observations']['held'],circuit['observations']['dac'],'TRACK','EVALUATE','VDD','VSS','VCORE'],via_columns=2,substrate_net='PSUB')
    add_power(layout,top,g)
    # Local source access, not the upper rail, produced >100 mV clock-driver
    # bounce in extraction. Collect every source stripe through three Via1s.
    g['supply_source_access']=strengthen_sources(layout,top,g,{'VDD','VSS','VCORE'},widen_bus=True)
    strengthen_input(layout,top,g)
    if track_overlay:strengthen_track(layout,top,g)
    # Keep every mask polygon and instance; normalize the whole macro together.
    bounds=top.bbox();dx,dy=20000-bounds.left,20000-bounds.bottom;top.transform(db.Trans(dx,dy))
    for part in g['components']:part['origin_um']=[part['origin_um'][0]+dx*.001,part['origin_um'][1]+dy*.001]
    for p in g['terminals'].values():p['x_um']+=dx*.001;p['y_um']+=dy*.001
    for p in g['escapes']:p['location_um']=[p['location_um'][0]+dx*.001,p['location_um'][1]+dy*.001]
    for rail in g.get('upper_power',{}).values():
        rail['x0_um']+=dx*.001;rail['x1_um']+=dx*.001;rail['y_um']+=dy*.001
        if 'stems' in rail:rail['stems']=[x+dx*.001 for x in rail['stems']]
    for p in g.get('storage_returns',[]):p['x_um']+=dx*.001;p['y_um']+=dy*.001
    g['normalization_um']=[dx*.001,dy*.001];g['ports']=circuit['ports'];g['observations']=circuit['observations']
    g['placement_view']=placement_views(layout,top,g,circuit['ports'],work)
    g['inventory']=dict(Counter(p['kind'] for p in g['components']))
    g['width_um']=top.bbox().width()*.001;g['height_um']=top.bbox().height()*.001;g['bounding_area_um2']=top.bbox().area()*1e-6
    layout.write(str(work/'adc_sar_slice.gds'))
    (work/'adc_sar_slice.lvs.cir').write_text(g['schematic'])
    (work/'adc_sar_slice.schematic.spice').write_text(physical_reference(g,circuit['ports']))
    (work/'placement.json').write_text(json.dumps(g,indent=2)+'\n')
    print(json.dumps({k:g[k] for k in ('inventory','width_um','height_um','bounding_area_um2','placement_view')},indent=2))
    return g

if __name__=='__main__':
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--circuit',type=Path,required=True);parser.add_argument('--pdk',type=Path,required=True);parser.add_argument('--work',type=Path,required=True)
    default=Path(__file__).with_name('placement_order.json')
    parser.add_argument('--order',type=Path,default=default if default.exists() else None,help='retain this paired component order while resizing')
    parser.add_argument('--strengthen-track',action='store_true',help='test reinforced TRACK bus; requires new physical and electrical qualification')
    args=parser.parse_args();build(json.loads(args.circuit.read_text()),args.pdk.resolve(),args.work.resolve(),json.loads(args.order.read_text()) if args.order else None,args.strengthen_track)
