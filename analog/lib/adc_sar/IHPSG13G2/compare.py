#!/usr/bin/env python3
"""Compare independently measured conversions and both observed waveform grids.

This is an observed-node check, not a claim about every extracted internal node.
It never shifts waveforms to conceal physical timing differences.
"""
import argparse,bisect,csv,json,math
from pathlib import Path
from measure import measure

LIMITS=dict(code=0,acquisition_V=1e-3,hold_pp_V=1e-3,
            minimum_decision_margin_s=50e-12)

def waveform(left,right):
    def read(path):
        with (path/'trace.csv').open() as f:
            rows=[{k:float(v) for k,v in r.items()} for r in csv.DictReader(f)]
        if not rows or any(not all(map(math.isfinite,r.values())) for r in rows):raise ValueError('invalid waveform')
        times=[r['time_s'] for r in rows]
        if any(b<=a for a,b in zip(times,times[1:])):raise ValueError('unordered waveform')
        return times,rows
    ta,a=read(left);tb,b=read(right)
    if set(a[0])!=set(b[0]):raise ValueError('different observed-node sets')
    if abs(ta[-1]-tb[-1])>1e-16:raise ValueError('different waveform duration')
    start=max(ta[0],tb[0]);end=min(ta[-1],tb[-1]);grid=sorted({t for t in ta+tb if start<=t<=end})
    def value(times,rows,t,k):
        i=bisect.bisect_left(times,t)
        if i==0:return rows[0][k]
        if i==len(times):return rows[-1][k]
        u,v=rows[i-1],rows[i]
        return u[k]+(v[k]-u[k])*(t-times[i-1])/(times[i]-times[i-1])
    nodes={}
    for k in a[0]:
        if k=='time_s':continue
        errors=[value(ta,a,t,k)-value(tb,b,t,k) for t in grid]
        peak=max(map(abs,errors));index=next(i for i,e in enumerate(errors) if abs(e)==peak)
        integral=sum((y-x)*(e*e+e*f+f*f)/3 for x,y,e,f in zip(grid,grid[1:],errors,errors[1:]))
        nodes[k]=dict(peak_V=peak,peak_time_s=grid[index],rms_V=math.sqrt(integral/(end-start)))
    return dict(scope='only the exported observations, not all internal RC/PSP nodes; linear interpolation on the union of both grids, no time registration',
                start_s=start,end_s=end,points=len(grid),nodes=nodes,
                peak_V=max(n['peak_V'] for n in nodes.values()),rms_V=max(n['rms_V'] for n in nodes.values()))

def compare(left,right):
    a,b=measure(left),measure(right)
    if len(a['samples'])!=len(b['samples']):raise ValueError('different conversion counts')
    differences={k:0. for k in LIMITS}
    for u,v in zip(a['samples'],b['samples']):
        if u['sample']!=v['sample'] or u['input_V']!=v['input_V']:raise ValueError('different conversion stimuli')
        for k in differences:differences[k]=max(differences[k],abs(u[k]-v[k]))
    wave=waveform(left,right)
    scalar=all(differences[k]<=v for k,v in LIMITS.items())
    observed=wave['peak_V']<=.020 and wave['rms_V']<=.001
    return dict(status='PASS' if scalar and observed else 'FAIL',
                left=str(left.resolve()),right=str(right.resolve()),
                functional_status=[a['status'],b['status']],
                scalar_status='PASS' if scalar else 'FAIL',differences=differences,requirements=LIMITS,
                waveform_status='PASS' if observed else 'FAIL',waveform_requirements=dict(peak_V=.020,rms_V=.001),waveform=wave)

if __name__=='__main__':
    p=argparse.ArgumentParser(description=__doc__)
    p.add_argument('left',type=Path);p.add_argument('right',type=Path);p.add_argument('--output',type=Path,required=True)
    a=p.parse_args();r=compare(a.left,a.right);a.output.write_text(json.dumps(r,indent=2)+'\n');print(r['status'])
    raise SystemExit(r['status']!='PASS')
