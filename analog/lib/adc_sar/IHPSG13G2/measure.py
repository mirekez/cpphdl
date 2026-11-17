"""Independent waveform measurements, common to native and reference engines."""
import bisect,csv,json,math
from pathlib import Path

def measure(work):
    with (work/'trace.csv').open() as f:
        reader=csv.DictReader(f);rows=[{k:float(v) for k,v in r.items()} for r in reader]
    if len(rows)<2 or any(not all(map(math.isfinite,r.values())) for r in rows):raise ValueError('invalid electrical trace')
    times=[r['time_s'] for r in rows]
    if any(b<=a for a,b in zip(times,times[1:])):raise ValueError('unordered waveform')
    def at(t,k):
        i=bisect.bisect_left(times,t)
        if i==0:return rows[0][k]
        if i==len(times):raise ValueError('waveform ends before measurement')
        a,b=rows[i-1],rows[i];return a[k]+(b[k]-a[k])*(t-a['time_s'])/(b['time_s']-a['time_s'])
    def window(a,b):return [dict(time_s=a,**{k:at(a,k) for k in rows[0] if k!='time_s'})]+[r for r in rows if a<r['time_s']<b]+[dict(time_s=b,**{k:at(b,k) for k in rows[0] if k!='time_s'})]
    number=lambda v:float(v) if v not in ('True','False') else float(v=='True')
    with (work/'results.csv').open() as f:result=[{k:number(v) for k,v in r.items()} for r in csv.DictReader(f)]
    with (work/'decisions.csv').open() as f:decisions=[{k:float(v) if v not in ('True','False') else v=='True' for k,v in r.items()} for r in csv.DictReader(f)]
    if not result or len(decisions)!=8*len(result):raise ValueError('incomplete conversions')
    for r in result:
        sample=int(r['sample']);origin=20e-9+50e-9*sample;aperture=origin+8e-9+25e-12;end=origin+41e-9
        held=[x['held'] for x in window(aperture,end)];acquisition=abs(held[0]-r['input_V']);variation=max(held)-min(held)
        bits=[d for d in decisions if int(d['sample'])==sample]
        if [int(d['bit']) for d in bits]!=list(range(8)):raise ValueError('bad SAR decision inventory')
        invalid=0;minimum_margin=math.inf;code=0
        for d in bits:
            t=d['time_s'];v,core,p,n,io=[at(t,k) for k in ('DECISION','VCORE','positive','negative','VDD')]
            trial=code|(128>>int(d['bit']))
            if d['trial']!=trial:raise ValueError('SAR trial did not follow the physical decision')
            if v>.5*core:code=trial
            resolved=lambda t:(at(t,'DECISION')<.3*at(t,'VCORE') and at(t,'negative')>.7*at(t,'VDD') and at(t,'positive')<.3*at(t,'VDD')) if v<.5*core else (at(t,'DECISION')>.7*at(t,'VCORE') and at(t,'positive')>.7*at(t,'VDD') and at(t,'negative')<.3*at(t,'VDD'))
            valid=resolved(t);invalid+=not valid
            evaluation=origin+11e-9+4e-9*d['bit'];stable=t
            for point in reversed(window(evaluation,t)):
                if not resolved(point['time_s']):break
                stable=point['time_s']
            minimum_margin=min(minimum_margin,t+25e-12-stable if valid else 0.)
        ideal=max(0,min(255,int(r['input_V']/3.3*256)))
        if r['code']!=code or r['ideal']!=ideal or r['code_error']!=abs(code-ideal):raise ValueError('reported code differs from independently reconstructed physical decisions')
        cycle=window(origin,origin+50e-9);io_current=[(x['io_source']-x['VDD'])/.25 for x in cycle];core_current=[(x['core_source']-x['VCORE'])/10 for x in cycle]
        power=[x['io_source']*i+x['core_source']*c for x,i,c in zip(cycle,io_current,core_current)]
        energy=sum((a['time_s']-b['time_s'])*(pa+pb)/2 for a,b,pa,pb in zip(cycle[1:],cycle,power[1:],power))
        r.update(acquisition_V=acquisition,hold_pp_V=variation,invalid_decisions=invalid,minimum_decision_margin_s=minimum_margin,full_cycle_energy_J=energy,full_cycle_power_W=energy/50e-9,io_peak_A=max(io_current),core_peak_A=max(core_current),io_min_V=min(x['VDD'] for x in cycle),core_min_V=min(x['VCORE'] for x in cycle))
        r['pass']=acquisition<=.020 and variation<=.020 and r['code_error']<=2 and not invalid and minimum_margin>=25e-12-1e-18 and r['io_min_V']>=3 and r['core_min_V']>=1.08
    receipt=dict(status='PASS' if all(r['pass'] for r in result) else 'FAIL',samples=result,measurement_scope='8 ns acquisition, 2 ns DAC settle + 2 ns evaluation, 25 ps setup, 50 ns full-cycle energy; power and peak current have no specified budget')
    (work/'measurements.json').write_text(json.dumps(receipt,indent=2)+'\n');return receipt

if __name__=='__main__':
    import argparse
    p=argparse.ArgumentParser(description=__doc__);p.add_argument('work',type=Path);a=p.parse_args();print(json.dumps(measure(a.work),indent=2))
