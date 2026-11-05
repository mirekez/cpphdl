"""Spatial buffer trees for routability, without setup/hold optimization.

Only core digital input terminals are moved. Original analog terminals remain
on their original nets. Every relocated load is checked to reach its original
net through non-inverting IHP buffers; clock skew is explicitly unqualified.
Run in OpenROAD's Python interpreter.
"""
import bisect
from collections import defaultdict
import json
import math
import os
from pathlib import Path
import re
import sys
import time
sys.path.insert(0,str(Path(__file__).resolve().parent))
from shared_sparse_legalize import FreeRow,validate


def morton(x,y):
    x=max(0,x//1000);y=max(0,y//1000)
    result=0
    for bit in range(16):
        result|=((x>>bit)&1)<<(2*bit)
        result|=((y>>bit)&1)<<(2*bit+1)
    return result


def run():
    import odb
    started=time.monotonic()
    work=Path(os.environ['SHARED_WORK'])
    pdk=Path(os.environ['SHARED_PDK'])/'ihp-sg13g2/libs.ref/sg13g2_stdcell'
    liberty=(pdk/'lib/sg13g2_stdcell_typ_1p20V_25C.lib').read_text()
    cell=liberty.split('cell (sg13g2_buf_8)',1)[1].split('\n  cell (',1)[0]
    if not re.search(r'pin\s*\(X\).*?function\s*:\s*"A"',cell,re.S):
        raise ValueError('buffer has no verified identity function')
    database=odb.dbDatabase.create()
    odb.read_db(database,str(work/'connected.odb'))
    block=database.getChip().getBlock()
    master=database.findMaster('sg13g2_buf_8')
    pitch,height,padding=480,3780,1
    rows={}
    for r in block.getRows():
        x,y=r.getOrigin()
        intervals,orientation=rows.setdefault(y,([],r.getOrient()))
        intervals.append((x//pitch,x//pitch+r.getSiteCount()))
    for spans,_ in rows.values():spans.sort()
    occupied=defaultdict(list)
    all_cells=[]
    for inst in block.getInsts():
        if inst.isFixed():continue
        b=inst.getBBox()
        occupied[b.yMin()].append((b.xMin()//pitch-padding,b.xMax()//pitch+padding))
        all_cells.append((inst,0,0,0,inst.getName()))
    ys=sorted(rows)
    free=[]
    for y in ys:
        spans=sorted(occupied[y])
        remaining=[]
        for low,high in rows[y][0]:
            end=low
            for a,b in spans:
                if b<=low:continue
                if a>=high:break
                if a>end:remaining.append((end,min(a,high)))
                end=max(end,b)
            if end<high:remaining.append((end,high))
        free.append(FreeRow(remaining))
    upstream={}
    inserted=[]
    def buffer(parent,loads):
        number=len(inserted)
        child=odb.dbNet_create(block,f'shared_fanout_net_{number}')
        child.setSigType(parent.getSigType())
        inst=odb.dbInst_create(block,master,f'shared_fanout_buf_{number}')
        inst.findITerm('A').connect(parent)
        inst.findITerm('X').connect(child)
        inst.findITerm('VDD').connect(block.findNet('VDD'))
        inst.findITerm('VSS').connect(block.findNet('VSS'))
        boxes=[p.getInst().getBBox() for p in loads]
        x=round(sum((b.xMin()+b.xMax())/2 for b in boxes)/len(boxes))
        y=round(sum((b.yMin()+b.yMax())/2 for b in boxes)/len(boxes))
        middle=min(len(ys)-1,bisect.bisect_left(ys,y))
        width=master.getWidth()//pitch
        where=None
        for distance in range(len(ys)):
            for i in ([middle] if distance==0 else [middle-distance,middle+distance]):
                if not 0<=i<len(ys):continue
                left=free[i].reserve(width+2*padding,round(x/pitch)-padding)
                if left is not None:
                    where=((left+padding)*pitch,ys[i],rows[ys[i]][1])
                    break
            if where:break
        if where is None:raise ValueError('no legal buffer site')
        inst.setOrient(where[2]);inst.setLocation(where[0],where[1]);inst.setPlacementStatus('PLACED')
        for pin in loads:pin.connect(child)
        upstream[child.getId()]=parent.getId()
        inserted.append(inst)
        all_cells.append((inst,0,0,0,inst.getName()))
        return inst.findITerm('A')
    selected=[]
    # Snapshot before adding buffers; internal analog/passive nets are excluded.
    for net in block.getNets():
        if str(net.getSigType()) in ('POWER','GROUND') or net.getITermCount()<=128:continue
        loads=[p for p in net.getITerms() if str(p.getIoType())=='INPUT'
               and not p.getInst().getName().startswith(('analog_d','pad_'))]
        if len(loads)>128:selected.append((net,loads))
    changed=0
    report=[]
    for net,original in selected:
        root=net.getId()
        loads=original
        before=len(loads)
        while len(loads)>128:
            loads=sorted(loads,key=lambda p:morton(p.getInst().getBBox().xMin(),p.getInst().getBBox().yMin()))
            loads=[buffer(net,loads[i:i+128]) for i in range(0,len(loads),128)]
        # Each inserted buffer implements X=A; following its parent net proves
        # static identity for every original digital sink, including clocks.
        # Read actual final buffer input nets, including higher tree levels.
        upstream={i.findITerm('X').getNet().getId():i.findITerm('A').getNet().getId() for i in inserted}
        for pin in original:
            current=pin.getNet().getId()
            visited=set()
            while current in upstream:
                if current in visited:raise ValueError('cyclic buffer tree')
                visited.add(current)
                current=upstream[current]
            if current!=root:raise ValueError('buffer tree changed a load source')
        report.append(dict(net=net.getName(),original_core_loads=before,root_core_loads=len(loads)))
        changed+=before
        print(f'Buffered {net.getName()}: {before} core loads; total buffers {len(inserted)}',flush=True)
    validate(rows,all_cells,pitch,height,padding)
    odb.write_db(database,str(work/'buffered.odb'))
    receipt=dict(status='buffered_for_routing',full_chip_qualified=False,timing_gate=False,
                 clock_skew_qualified=False,inserted_buffers=len(inserted),checked_sink_sources=changed,
                 trees=report,seconds=time.monotonic()-started)
    (work/'fanout.json').write_text(json.dumps(receipt,indent=2)+'\n')
    print(json.dumps({k:receipt[k] for k in ('status','inserted_buffers','checked_sink_sources','seconds')}),flush=True)

if __name__=='__main__':run()
