"""Batch row subtraction for many physical leaves (OpenROAD Python entry).

Bucket each fixed macro into intersecting original rows, merge its blocked
site intervals once, and retain legal remaining row spans. This avoids
repeated rescanning of fragments as tens of thousands of leaves are added.
"""
import bisect
from collections import defaultdict
import json
import math
import os
from pathlib import Path
import time


def free_spans(count, blocked, minimum=10):
    end=0
    result=[]
    for low,high in sorted(blocked):
        low=max(0,min(count,low))
        high=max(0,min(count,high))
        if low>end and low-end>=minimum:
            result.append((end,low))
        end=max(end,high)
    if count-end>=minimum:
        result.append((end,count))
    return result


def run():
    import odb
    started=time.monotonic()
    work=Path(os.environ['SHARED_WORK'])
    database=odb.dbDatabase.create()
    odb.read_db(database,str(work/'floorplan.odb'))
    block=database.getChip().getBlock()
    rows=[]
    for row in block.getRows():
        x,y=row.getOrigin()
        if str(row.getDirection())!='HORIZONTAL':
            raise ValueError('unsupported placement row direction')
        rows.append(dict(row=row,x=x,y=y,height=row.getSite().getHeight(),count=row.getSiteCount(),
                         spacing=row.getSpacing(),site=row.getSite(),orient=row.getOrient(),name=row.getName(),direction=row.getDirection()))
    rows.sort(key=lambda row:row['y'])
    ys=[r['y'] for r in rows]
    if len(ys)!=len(set(ys)):
        raise ValueError('batch subtraction requires original unsplit rows')
    intervals=defaultdict(list)
    halo=1000
    macro_count=0
    for inst in block.getInsts():
        if not inst.getMaster().isBlock():
            continue
        if not inst.isFixed():
            raise ValueError('unplaced physical leaf: '+inst.getName())
        box=inst.getBBox()
        low,high=box.yMin()-halo,box.yMax()+halo
        first=max(0,bisect.bisect_right(ys,low)-1)
        stop=bisect.bisect_left(ys,high)
        for i in range(first,stop):
            r=rows[i]
            if r['y']+r['height']<=low:
                continue
            left=math.floor((box.xMin()-halo-r['x'])/r['spacing'])
            right=math.ceil((box.xMax()+halo-r['x'])/r['spacing'])
            intervals[i].append((left,right))
        macro_count+=1
    count=0
    for i,r in enumerate(rows):
        if i not in intervals:
            count+=1
            continue
        for j,(a,b) in enumerate(free_spans(r['count'],intervals[i])):
            odb.dbRow_create(block,r['name']+'_shared_'+str(j),r['site'],r['x']+a*r['spacing'],r['y'],
                             r['orient'],r['direction'],b-a,r['spacing'])
            count+=1
        odb.dbRow_destroy(r['row'])
    pending=work/'floorplan-cut.odb'
    odb.write_db(database,str(pending))
    pending.replace(work/'floorplan.odb')
    odb.write_def(block,str(work/'floorplan.def'))
    report=dict(status='rows_cut',original_rows=len(rows),remaining_rows=count,physical_blocks=macro_count,
                halo_um=1,minimum_span_sites=10,seconds=time.monotonic()-started)
    (work/'row-cut.json').write_text(json.dumps(report,indent=2)+'\n')
    print(json.dumps(report),flush=True)

if __name__=='__main__':
    run()
