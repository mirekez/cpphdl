"""Legalize shared-die cells using free row intervals, without a full site grid.

Preserves every physical leaf and connectivity in the global-placement ODB.
This is geometric legalization, not a timing optimizer. The independent final
sweep checks original row containment, orientation, spacing and cell overlap.
Run with OpenROAD -python.
"""
import bisect
from collections import defaultdict
import json
import math
import os
from pathlib import Path
import time


class FreeRow:
    def __init__(self, intervals):
        self.free=sorted(intervals)
        self.maximum=max((b-a for a,b in self.free),default=0)

    def reserve(self, width, target):
        if width>self.maximum:
            return None
        best=None
        for i,(a,b) in enumerate(self.free):
            if b-a<width:
                continue
            x=max(a,min(b-width,target))
            cost=abs(x-target)
            if best is None or cost<best[0]:
                best=(cost,i,x,a,b)
                if cost==0:
                    break
        if best is None:
            raise ValueError('free-row maximum is inconsistent')
        _,i,x,a,b=best
        remainder=[]
        if x>a:
            remainder.append((a,x))
        if x+width<b:
            remainder.append((x+width,b))
        self.free[i:i+1]=remainder
        self.maximum=max((b-a for a,b in self.free),default=0)
        return x


def validate(rows,cells,pitch,height,padding):
    grouped=defaultdict(list)
    for inst,_,_,_,_ in cells:
        box=inst.getBBox()
        y=box.yMin()
        if y not in rows or box.yMax()-box.yMin()!=height or box.xMin()%pitch:
            raise ValueError('cell off placement grid: '+inst.getName())
        intervals,orientation=rows[y]
        if inst.getOrient()!=orientation:
            raise ValueError('wrong row orientation: '+inst.getName())
        low,high=box.xMin()//pitch-padding,box.xMax()//pitch+padding
        i=bisect.bisect_right(intervals,(low,math.inf))-1
        if i<0 or low<intervals[i][0] or high>intervals[i][1]:
            raise ValueError('cell outside original legal row: '+inst.getName())
        grouped[y].append((low,high,inst.getName()))
    for spans in grouped.values():
        spans.sort()
        for a,b in zip(spans,spans[1:]):
            if a[1]>b[0]:
                raise ValueError('overlapping cells: '+a[2]+' / '+b[2])


def run():
    import odb
    started=time.monotonic()
    work=Path(os.environ['SHARED_WORK'])
    database=odb.dbDatabase.create()
    odb.read_db(database,str(work/'global.odb'))
    block=database.getChip().getBlock()
    rows={}
    pitch,height,padding=480,3780,1
    for row in block.getRows():
        x,y=row.getOrigin()
        if row.getSite().getHeight()!=height or row.getSpacing()!=pitch or x%pitch:
            raise ValueError('unsupported placement site')
        intervals,orientation=rows.setdefault(y,([],row.getOrient()))
        if orientation!=row.getOrient():
            raise ValueError('mixed row orientations')
        intervals.append((x//pitch,x//pitch+row.getSiteCount()))
    for intervals,_ in rows.values():
        intervals.sort()
    ys=sorted(rows)
    available=[FreeRow(rows[y][0]) for y in ys]
    cells=[]
    fixed=0
    for inst in block.getInsts():
        if inst.isFixed():
            fixed+=1
            continue
        master=inst.getMaster()
        if master.isBlock() or master.getHeight()!=height or master.getWidth()%pitch:
            raise ValueError('unsupported movable physical cell '+inst.getName())
        box=inst.getBBox()
        cells.append((inst,master.getWidth()//pitch,box.xMin(),box.yMin(),inst.getName()))
    if len(cells)!=879909:
        raise ValueError('changed movable standard-cell inventory')
    # Large cells reserve space first. Within each width, retain the global
    # placer's spatial ordering. There is no logical-module ownership fence.
    cells.sort(key=lambda c:(-c[1],c[3],c[2],c[4]))
    displacement_sum=0
    displacement_max=0
    for number,(inst,width,old_x,old_y,_) in enumerate(cells):
        target=round(old_x/pitch)-padding
        middle=min(len(ys)-1,max(0,bisect.bisect_left(ys,old_y)))
        if middle and abs(ys[middle-1]-old_y)<abs(ys[middle]-old_y):
            middle-=1
        position=None
        for distance in range(len(ys)):
            candidates=[middle] if distance==0 else [middle-distance,middle+distance]
            for index in candidates:
                if not 0<=index<len(ys):
                    continue
                x=available[index].reserve(width+2*padding,target)
                if x is not None:
                    position=((x+padding)*pitch,ys[index],rows[ys[index]][1])
                    break
            if position is not None:
                break
        if position is None:
            raise ValueError('legal row space exhausted; no cells are dropped')
        x,y,orientation=position
        inst.setOrient(orientation)
        inst.setLocation(x,y)
        inst.setPlacementStatus('PLACED')
        displacement=abs(x-old_x)+abs(y-old_y)
        displacement_sum+=displacement
        displacement_max=max(displacement_max,displacement)
        if (number+1)%100000==0:
            print(f'Legalized {number+1} cells, {time.monotonic()-started:.1f}s',flush=True)
    validate(rows,cells,pitch,height,padding)
    odb.write_db(database,str(work/'placed.odb'))
    odb.write_def(block,str(work/'placed.def'))
    report=dict(status='geometrically_legal',full_chip_qualified=False,timing_gate=False,
                method='sparse row intervals; independent containment and overlap sweep',
                standard_cells=len(cells),fixed_physical_instances=fixed,
                mean_displacement_um=displacement_sum/len(cells)/1000,
                max_displacement_um=displacement_max/1000,seconds=time.monotonic()-started)
    (work/'legalization.json').write_text(json.dumps(report,indent=2)+'\n')
    print(json.dumps(report),flush=True)

if __name__=='__main__':
    run()
