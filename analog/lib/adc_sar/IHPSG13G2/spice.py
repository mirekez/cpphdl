#!/usr/bin/env python3
"""Independent, closed-loop process reference; every bit reads its own PSP latch.

Causal prefix replay avoids ngspice PSP pause/resume state corruption. Only
already decided control transitions are replayed; future decisions are unknown.
"""
import argparse,csv,json,math,os,sys,subprocess,re
from pathlib import Path
from process import statements
from provenance import digest,verify,receipt


def run(netlist,observations,pdk,work,levels,corner='mos_tt',temperature=27,step=20e-12,ngspice=Path('/usr/bin/ngspice')):
    if work.exists() and any(work.iterdir()):raise ValueError('reference evidence directory must be empty')
    if not levels or any(not math.isfinite(v) or not 0<=v<=3.3 for v in levels):raise ValueError('invalid reference input levels')
    if not math.isfinite(step) or not 0<step<=100e-12:raise ValueError('invalid reference timestep')
    if list(observations)!=['held','dac','positive','negative']:raise ValueError('incorrect reference observation order')
    work.mkdir(parents=True,exist_ok=True);models=pdk/'ihp-sg13g2/libs.tech/ngspice/models'
    files={Path(__file__).resolve(),Path(__file__).with_name('process.py').resolve(),netlist,ngspice}
    files.update(p for p in models.parent.rglob('*') if p.is_file() and (p.suffix in ('.lib','.osdi') or p.name=='.spiceinit'))
    hashes={str(p.resolve()):digest(p) for p in files}
    (work/'inputs.json').write_text(json.dumps(hashes,indent=2)+'\n')
    text=netlist.read_text();sub=next(x.split() for x in statements(text) if x.lower().startswith('.subckt '));ports=sub[2:]
    names=['TRACK','EVALUATE','CLEAR']+[f'BIT_N{i}' for i in range(8)]
    waves={n:[(0,int(n in ('TRACK','CLEAR') or n.startswith('BIT_N') and n!='BIT_N7'))] for n in names}
    def drive(name,t,value):
        old=waves[name][-1][1]
        if old!=value:waves[name]+=[(t,old),(t+50e-12,value)]
    def bits(trial,t):
        for i in range(8):drive(f'BIT_N{i}',t,int(not trial&(1<<i)))
    points=[(0,levels[0])]
    for i,level in enumerate(levels[1:],1):points += [(20e-9+50e-9*i,levels[i-1]),(20e-9+50e-9*i+100e-12,level)]
    def pwl(p):return ' '.join(f'{t:.17g} {v:.17g}' for t,v in p)
    base=['ADC IHP independent closed-loop reference',f'.lib "{models}/cornerMOShv.lib" {corner}',f'.lib "{models}/cornerMOSlv.lib" {corner}',f'.lib "{models}/cornerRES.lib" res_typ',f'.lib "{models}/cornerCAP.lib" cap_typ',f'.temp {temperature}',
        '.options method=gear maxord=2 reltol=1e-7 abstol=1e-12 vntol=1e-9 num_threads=1',text,
        'XADC '+' '.join('0' if p=='VSS' else p for p in ports)+' '+sub[1],
        'VIO io_source 0 PWL(0 0 1n 3.3)','RIO io_source VDD .25','CIO VDD 0 20p',
        'VCORE core_source 0 PWL(0 0 1n 1.2)','RCORE core_source VCORE 10','CCORE VCORE 0 20p',
        'VIN source 0 PWL('+pwl(points)+')','RIN source IN 50','COUT DECISION 0 20f']
    # Match native observation deadlines exactly; interpolation across a
    # 50 ps sampling edge can otherwise invent sub-mV acquisition differences.
    deadlines=sorted({0.}|{28.025e-9+50e-9*s for s in range(len(levels))}|{32.975e-9+50e-9*s+4e-9*b for s in range(len(levels)) for b in range(8)})
    base.append('Vobserve observation_clock 0 PWL('+pwl([(t,float(i%2)) for i,t in enumerate(deadlines)])+')')
    vec=['v(xadc.'+n+')' for n in observations.values()]+['v('+n+')' for n in ('source','IN','VDD','VCORE','DECISION','io_source','core_source')]
    for i,name in enumerate(names):base += [f'Bdrive{i} drive{i} 0 V=v(VDD)*v(ctrl{i})',f'Rdrive{i} drive{i} {name} 50']
    base+=['.save '+' '.join(vec)]
    decisions=[];codes=[];control=[]
    env=dict(os.environ,PDK_ROOT=str(pdk),PDK='ihp-sg13g2',OMP_NUM_THREADS='1')
    env['SPICE_USERINIT_DIR']=str(models.parent)
    def prefix(stop,index,save=False):
        deck='\n'.join(base+[f'Vctrl{i} ctrl{i} 0 PWL('+pwl(waves[n])+')' for i,n in enumerate(names)])
        control=['.control','set num_threads=1','set numdgt=17','set wr_singlescale',f'tran {step:.17g} {stop:.17g} 0 {step:.17g} uic',
                 'let last=length(time)-1','print time[last] '+ ' '.join(v+'[last]' for v in vec)]
        if save:control.append('wrdata trace.dat '+' '.join(vec))
        path=work/f'prefix-{index}.cir';path.write_text(deck+'\n'+'\n'.join(control+['quit','.endc','.end','']))
        try:r=subprocess.run([str(ngspice),'-b',path.name],cwd=work,env=env,text=True,stdout=subprocess.PIPE,stderr=-2,timeout=600)
        except subprocess.TimeoutExpired as e:
            output=e.stdout or ''
            if isinstance(output,bytes):output=output.decode(errors='replace')
            (work/f'prefix-{index}.log').write_text(output+'\nReference exceeded 600 s timeout.\n')
            raise
        (work/f'prefix-{index}.log').write_text(r.stdout)
        if r.returncode or re.search(r'(?im)^Error|timestep too small|simulation interrupted',r.stdout):raise ValueError('ngspice prefix failed: '+str(path))
        values=[float(v) for v in re.findall(r'(?m)^\S+\[last\]\s*=\s*([-+0-9.eE]+)',r.stdout)]
        if len(values)!=12 or not all(map(math.isfinite,values)) or abs(values[0]-stop)>1e-16:raise ValueError('incomplete independent decision trace: '+str(path))
        return values
    for s,level in enumerate(levels):
        origin=20e-9+50e-9*s;drive('TRACK',origin,1);drive('EVALUATE',origin,0);drive('CLEAR',origin,0);bits(128,origin);drive('TRACK',origin+8e-9,0)
        code=invalid=0
        for bit in range(8):
            trial=code|(128>>bit);t=origin+9e-9+4e-9*bit
            drive('EVALUATE',t,0);bits(trial,t);drive('EVALUATE',t+2e-9,1)
            at=t+4e-9-25e-12;values=prefix(at,8*s+bit)
            v,core,positive,negative,io=values[9],values[8],values[3],values[4],values[7]
            valid=(v<.3*core and negative>.7*io and positive<.3*io) or (v>.7*core and positive>.7*io and negative<.3*io)
            invalid+=not valid
            decisions.append(dict(sample=s,bit=bit,time_s=at,trial=trial,decision_V=v,core_V=core,positive_V=positive,negative_V=negative,valid=valid))
            if v>.5*core:code=trial
            print(f'{corner} {temperature}C sample {s} bit {bit}: {v:.6g} V -> {code}',flush=True)
        ideal=max(0,min(255,int(level/3.3*256)))
        codes.append(dict(sample=s,input_V=level,code=code,ideal=ideal,code_error=abs(code-ideal),invalid_decisions=invalid))
        drive('EVALUATE',origin+41e-9,0)
    prefix(20e-9+50e-9*len(levels),'final',True)
    for name,wave in waves.items():
        for t,v in wave:control.append(dict(driver=name,time_s=t,value=v))
    rows=[]
    for line in (work/'trace.dat').read_text().splitlines():
        f=list(map(float,line.split()))
        if len(f)!=12 or not all(map(math.isfinite,f)):raise ValueError('invalid process waveform')
        rows.append(f)
    header=['time_s',*observations,'source','IN','VDD','VCORE','DECISION','io_source','core_source']
    with (work/'trace.csv').open('w') as f:w=csv.writer(f);w.writerow(header);w.writerows(rows)
    import bisect
    times=[r[0] for r in rows]
    if any(min(abs(t-d) for t in times)>1e-16 for d in deadlines if d>0):raise ValueError('reference missed an exact observation deadline')
    def value(t,col):
        i=bisect.bisect_left(times,t)
        if i==0:return rows[0][col]
        if i==len(rows):raise ValueError('incomplete waveform')
        a,b=rows[i-1],rows[i];return a[col]+(b[col]-a[col])*(t-a[0])/(b[0]-a[0])
    for r in codes:
        t=28.025e-9+50e-9*r['sample'];end=61e-9+50e-9*r['sample'];held=[value(t,1)]+[a[1] for a in rows if t<=a[0]<=end]+[value(end,1)]
        r.update(acquisition_V=abs(held[0]-r['input_V']),hold_pp_V=max(held)-min(held))
        r['pass']=r['acquisition_V']<=.020 and r['hold_pp_V']<=.020 and r['code_error']<=2 and r['invalid_decisions']==0
    for filename,data in [('results.csv',codes),('decisions.csv',decisions),('controls.csv',control)]:
        with (work/filename).open('w') as f:w=csv.DictWriter(f,fieldnames=list(data[0]));w.writeheader();w.writerows(data)
    result=dict(status='PASS' if all(r['pass'] for r in codes) else 'FAIL',engine='ngspice PSP/R3/MIM closed-loop causal-prefix reference',corner=corner,temperature_C=temperature,step_s=step,results=codes)
    verify(hashes)
    (work/'checks.json').write_text(json.dumps(result,indent=2)+'\n')
    receipt(work/'manifest.json',hashes,[p for p in work.iterdir() if p.is_file() and p.name not in ('manifest.json','inputs.json')],status=result['status'],qualified=False)
    return result

if __name__=='__main__':
    p=argparse.ArgumentParser(description=__doc__)
    for name in ('netlist','mapping','pdk','work'):p.add_argument('--'+name,type=Path,required=True)
    p.add_argument('--corner',default='mos_tt');p.add_argument('--temperature',type=float,default=27);p.add_argument('--step',type=float,default=20e-12);p.add_argument('--levels',type=float,nargs='+',default=[.85,1.15,1.45,1.75,2.05])
    a=p.parse_args();m=json.loads(a.mapping.read_text())['observations'];m={k:v['extracted_node'] if isinstance(v,dict) else v for k,v in m.items()}
    if a.work.exists() and any(a.work.iterdir()):raise ValueError('reference evidence directory must be empty')
    try:r=run(a.netlist.resolve(),m,a.pdk.resolve(),a.work.resolve(),a.levels,a.corner,a.temperature,a.step)
    except Exception as e:
        a.work.mkdir(parents=True,exist_ok=True)
        r=dict(status='ERROR',qualified=False,error=f'{type(e).__name__}: {e}')
        (a.work/'checks.json').write_text(json.dumps(r,indent=2)+'\n')
    print(json.dumps(r,indent=2));sys.exit(r['status']!='PASS')
