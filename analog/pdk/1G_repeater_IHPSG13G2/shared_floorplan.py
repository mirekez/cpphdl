"""Prepare one connected die from digital ODB and all physical analog leaves.

Analog leaves and SRAM have local geometric constraints. Digital logic is
replaced in the remaining common rows, with no per-module fences.
"""
import argparse
import hashlib
import json
import math
from pathlib import Path
import re


def quote(value):
    value=str(value)
    if any(c in value for c in '{}\\\n\r'):
        raise ValueError('unsafe Tcl atom')
    return '{'+value+'}'


def digital_bit(name):
    m=re.fullmatch(r'(\w+)\[(\d+)\]\[(\d+)\]',name)
    if not m:
        return name
    widths={'trial_out':8,'comparisons_in':16,'main_p_out':4,'main_n_out':4,'history_p_out':4,'history_n_out':4}
    count=128 if m[1]=='trial_out' else 8
    # sv2v packs ascending unpacked [N] arrays with element zero at MSB.
    return f'{m[1]}[{(count-1-int(m[2]))*widths[m[1]]+int(m[3])}]'


def pack(instances,masters,side):
    # Spread physical leaves over vertical bands separated by movable logic
    # rows. Logical module boundaries have no physical fences or boxes.
    largest=max(m['width_um'] for m in masters.values())
    band=math.ceil(max(640,largest)/.48)*.48
    channel=180
    columns=int((side-80+channel)//(band+channel))
    if columns<1:
        return None
    pitch=(side-80)/columns
    bounds=[]
    for row in range(2):
        for col in range(4):
            x=side/2-2*442.61+col*442.61
            y=side/2-425.37+row*425.37
            bounds.append((x-15,y-15,x+402.61+15,y+385.37+15))
    positions={}
    ordered=sorted(instances,key=lambda d:(-masters[d['master']]['height_um'],d['owner'],d['source_id'],d['name']))
    col=0
    x0=40
    x=x0
    y=40
    height=0
    for d in ordered:
        m=masters[d['master']]
        w,h=m['width_um'],m['height_um']
        while True:
            if x+w>x0+band:
                x=x0
                y+=height+4
                height=0
            if y+h>side-40:
                col+=1
                if col>=columns:
                    return None
                x0=40+col*pitch
                x=x0
                y=40
                height=0
            collided=[b for b in bounds if x < b[2] and x+w>b[0] and y<b[3] and y+h>b[1]]
            if collided:
                x=max(b[2] for b in collided)+4
                continue
            break
        positions[d['name']]=[round((x+320)*1000),round((y+320)*1000)]
        x+=w+4
        height=max(height,h)
    memories=[[round((b[0]+15+320)*1000),round((b[1]+15+320)*1000)] for b in bounds]
    return positions,memories


def run(args):
    args.work.mkdir(parents=True,exist_ok=True)
    leaves=json.loads((args.leaves/'leaves.json').read_text())
    plan=json.loads(args.plan.read_text())
    side=math.ceil(math.sqrt(leaves['area_um2']/.62+10.5e6/.5)/200)*200
    while True:
        packed=pack(leaves['instances'],leaves['masters'],side)
        if packed:
            break
        side+=200
    positions,memories=packed
    (args.work/'floorplan.json').write_text(json.dumps(dict(core_side_um=side,die_side_um=side+640,
        physical_analog_leaves=len(positions),fixed_logic_modules=0,timing_gate=False,
        analog_area_um2=leaves['area_um2'],memory_locations_dbu=memories),indent=2)+'\n')
    # Canonical nets retain the real mapped BTerm identity for all analog/RTL
    # crossings. Multiple analog pins on the same wire share one OpenDB net.
    names={}
    digital={}
    for p in plan['ports']:
        if p['digital']:
            name=digital_bit(p['digital'])
            if p['node'] in digital and digital[p['node']]!=name:
                raise ValueError('aliased digital boundary needs explicit reconciliation')
            digital[p['node']]=name
            names[p['node']]=name
    ports={p['name']:p['node'] for p in plan['ports']}
    names.update({ports['VDD']:'AVDD',ports['VLOGIC']:'VDD',ports['VSS']:'VSS'})
    for lane in range(8):
        for polarity in ('P','N'):
            pad=f'mdi_{"a" if lane<4 else "b"}_{lane%4}_{polarity.lower()}'
            names[ports[f'{polarity}_{lane}']]=pad
            names[ports[f'SENSE_{polarity}_{lane}']]=pad+'_core'
    def net(node):
        return names.get(node,'analog_n'+str(node))
    lines=['# Generated from actual electrical nodes and physical leaf pins.',
           'set block [ord::get_db_block]', 'set db [ord::get_db]',
           'proc shared_net {name} {',
           '  set block [ord::get_db_block]',
           '  set term [$block findBTerm $name]',
           '  if {$term != "NULL"} {return [$term getNet]}',
           '  set result [$block findNet $name]',
           '  if {$result == "NULL"} {set result [odb::dbNet_create $block $name]}',
           '  return $result', '}',
           'proc shared_connect {inst pin net} {',
           '  set terminal [$inst findITerm $pin]',
           '  if {$terminal == "NULL"} {error "Missing physical pin $pin"}',
           '  $terminal connect [shared_net $net]', '}',
           'set digital_boundaries 0']
    for node,name in sorted(digital.items()):
        lines += [f'if {{[$block findBTerm {quote(name)}] == "NULL"}} {{error "Missing digital boundary {name}"}}', 'incr digital_boundaries']
    records=[]
    for d in leaves['instances']:
        x,y=positions[d['name']]
        pins=' '.join(quote(v) for pin,node in d['connections'].items() for v in (pin,net(node)))
        records.append(f"{quote(d['name'])} {quote(d['master'])} {x} {y} {{{pins}}}")
    (args.work/'devices.list').write_text('\n'.join(records)+'\n')
    lines += ['set device_file [open $::env(SHARED_WORK)/devices.list r]',
              'set imported 0', 'while {[gets $device_file record] >= 0} {',
              '  lassign $record name master x y pins',
              '  set inst [odb::dbInst_create $block [$db findMaster $master] $name]',
              '  $inst setLocation $x $y', '  $inst setPlacementStatus LOCKED',
              '  foreach {pin net} $pins {shared_connect $inst $pin $net}',
              '  incr imported',
              '  if {$imported % 10000 == 0} {puts "Imported $imported analog leaves"}',
              '}', 'close $device_file']
    lines += ['set memories {}','foreach inst [$block getInsts] {',
              '  if {[[$inst getMaster] getName] == "RM_IHPSG13_2P_1024x16_c2_bm_bist"} {lappend memories [$inst getName]}', '}',
              'if {[llength $memories] != 8} {error "Missing SRAM"}',
              'set coordinates {'+' '.join('{%d %d}'%tuple(xy) for xy in memories)+'}',
              'foreach name [lsort $memories] xy $coordinates {',
              '  set inst [$block findInst $name]', '  $inst setPlacementStatus PLACED', '  $inst setOrient R0',
              '  $inst setLocation {*}$xy', '  $inst setPlacementStatus LOCKED', '}']
    # This stage executes after padframe.py generates this exact die boundary.
    lines.append('source $::env(SHARED_WORK)/pads.tcl')
    (args.work/'devices.tcl').write_text('\n'.join(lines)+'\n')
    nodes={str(n):net(n) for d in leaves['instances'] for n in d['connections'].values()}
    (args.work/'nodes.json').write_text(json.dumps(nodes,indent=2)+'\n')
    # Pad net aliases are resolved against the same physical top-level nets.
    aliases={'vdd':'VDD','vss':'VSS','iovdd':'AVDD','iovss':'VSS',
             'ref_clock_core':'ref_clk','reset_core':'reset','enable_core':'enable_in','active_core':'active_out'}
    (args.work/'pad_aliases.json').write_text(json.dumps(aliases,indent=2)+'\n')
    print(json.dumps(dict(core_side_um=side,die_side_um=side+640,analog_leaves=len(positions),digital_boundaries=len(digital))))


def pads(args):
    data=json.loads(args.padframe.read_text())
    aliases=json.loads((args.work/'pad_aliases.json').read_text())
    rows=[]
    for p in data['placements']:
        rows += [f'set inst [odb::dbInst_create $block [$db findMaster {quote(p["cell"])}] {quote("pad_"+p["name"])}]',
                 f'$inst setOrient R{p["rotation"]}',
                 # OpenDB origin is the transformed master origin, matching GDS.
                 f'$inst setOrigin {round(p["x_um"]*1000)} {round(p["y_um"]*1000)}',
                 '$inst setPlacementStatus LOCKED']
        for pin,name in p['nets'].items():
            rows.append(f'shared_connect $inst {quote(pin)} {quote(aliases.get(name,name))}')
    (args.work/'pads.tcl').write_text('\n'.join(rows)+'\n')

if __name__=='__main__':
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--work',type=Path,required=True)
    parser.add_argument('--leaves',type=Path)
    parser.add_argument('--plan',type=Path)
    parser.add_argument('--padframe',type=Path)
    args=parser.parse_args()
    (pads if args.padframe else run)(args)
