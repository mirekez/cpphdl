#!/usr/bin/env python3
"""Unshifted all-node waveform convergence with explicit numerical gates."""
import argparse
from array import array
import bisect
import csv
import json
import math
from pathlib import Path
import subprocess

LIMITS=dict(maximum_voltage_difference_V=.020,held_voltage_difference_V=.001,
            maximum_rms_difference_V=.001,maximum_edge_difference_s=2e-12,
            maximum_energy_relative_difference=.01,maximum_charge_relative_difference=.01,
            maximum_peak_current_relative_difference=.05)

def load_trace(path):
    with path.open() as f:
        reader=csv.reader(f);header=next(reader);columns={k:array('d') for k in header}
        for row in reader:
            for key,value in zip(header,row):columns[key].append(float(value))
    times=columns['time_s']
    if len(times)<2 or any(b<=a for a,b in zip(times,times[1:])):raise ValueError('trace timestamps must increase')
    if any(not math.isfinite(v) for values in columns.values() for v in values):raise ValueError('nonfinite waveform')
    return columns

def crossings(times,values,level):
    result=[]
    for i in range(1,len(times)):
        a,b=values[i-1],values[i]
        if (a<level<=b) or (a>level>=b):
            result.append((1 if b>a else -1,times[i-1]+(level-a)/(b-a)*(times[i]-times[i-1])))
    return result

def rail_integrals(trace):
    t=trace['time_s'];result={}
    for rail,source,r in [('io','source',.25),('core','logic_source',10.)]:
        node='vdd' if rail=='io' else 'core'
        current=[(a-b)/r for a,b in zip(trace[source],trace[node])]
        charge=sum((b-a)*(x+y)/2 for a,b,x,y in zip(t,t[1:],current,current[1:]))
        power=[a*b for a,b in zip(current,trace[source])]
        energy=sum((b-a)*(x+y)/2 for a,b,x,y in zip(t,t[1:],power,power[1:]))
        result[rail]=dict(charge_C=charge,energy_J=energy,peak_A=max(current))
    return result

def compare(coarse,fine):
    a=load_trace(coarse);b=load_trace(fine);t=a['time_s'];u=b['time_s']
    nodes=[k for k in a if k.startswith('node')]
    if not nodes or set(a)!=set(b):raise ValueError('missing or mismatched node inventory')
    if abs(t[0]-u[0])>1e-18 or abs(t[-1]-u[-1])>1e-18:raise ValueError('incomplete common waveform interval')
    # Only snap binary-rounding differences in the SAME event timestamp. No
    # waveform shifting, edge removal or omission of switching intervals.
    interpolation=[]
    for x in t:
        i=bisect.bisect_left(u,x)
        if i<len(u) and abs(u[i]-x)<=1e-18:interpolation.append((i,i,0.));continue
        if i and abs(u[i-1]-x)<=1e-18:interpolation.append((i-1,i-1,0.));continue
        if not 0<i<len(u):raise ValueError('missing interpolation endpoint')
        interpolation.append((i-1,i,(x-u[i-1])/(u[i]-u[i-1])))
    per_node={};edge_max=0;edge_count=0;unmatched=[]
    for node in nodes+['held']:
        errors=[x-(b[node][lo]+f*(b[node][hi]-b[node][lo])) for x,(lo,hi,f) in zip(a[node],interpolation)]
        peak=max(map(abs,errors));index=max(range(len(errors)),key=lambda i:abs(errors[i]))
        rms=math.sqrt(sum((hi-lo)*(x*x+y*y)/2 for lo,hi,x,y in zip(t,t[1:],errors,errors[1:]))/(t[-1]-t[0]))
        record=dict(maximum_V=peak,rms_V=rms,worst_time_s=t[index])
        low,high=min(b[node]),max(b[node])
        if node in nodes and high-low>=.1:
            level=(low+high)/2;ca=crossings(t,a[node],level);cb=crossings(u,b[node],level)
            record.update(reference_crossings=len(cb),crossing_level_V=level)
            if len(ca)!=len(cb) or [x[0] for x in ca]!=[x[0] for x in cb]:unmatched.append(node)
            else:
                delta=max((abs(x[1]-y[1]) for x,y in zip(ca,cb)),default=0.)
                record['maximum_crossing_delta_s']=delta;edge_max=max(edge_max,delta);edge_count+=len(ca)
        per_node[node]=record
    pa,pb=rail_integrals(a),rail_integrals(b);power={}
    for rail in pa:
        power[rail]={key:abs(pa[rail][key]-pb[rail][key])/max(abs(pb[rail][key]),1e-30) for key in pa[rail]}
    result=dict(nodes=len(nodes),interval_s=[t[0],t[-1]],coarse_samples=len(t),fine_samples=len(u),per_node=per_node,
        maximum_voltage_difference_V=max(per_node[k]['maximum_V'] for k in nodes),
        held_voltage_difference_V=per_node['held']['maximum_V'],maximum_rms_difference_V=max(per_node[k]['rms_V'] for k in nodes),
        maximum_edge_difference_s=edge_max,matched_edges=edge_count,unmatched_edge_nodes=unmatched,
        maximum_energy_relative_difference=max(v['energy_J'] for v in power.values()),
        maximum_charge_relative_difference=max(v['charge_C'] for v in power.values()),
        maximum_peak_current_relative_difference=max(v['peak_A'] for v in power.values()),
        rail_relative_differences=power,coarse_rail_integrals=pa,fine_rail_integrals=pb)
    result['failed_gates']=[k for k,limit in LIMITS.items() if result[k]>limit]+(['edge_count'] if unmatched else [])
    return result

def run(bench,work):
    work.mkdir(parents=True,exist_ok=True)
    grids=['0.625','0.3125','0.15625']
    for step in grids:subprocess.run([str(bench),'wave_bdf_'+step,str(work)],check=True)
    solver_records=[]
    for step in grids:
        with (work/('wave_bdf_'+step+'_solver.csv')).open() as f:record=next(csv.DictReader(f))
        if int(record['inserted_edge_points'])<=0:raise RuntimeError('driver-edge refinement was not exercised')
        solver_records.append({k:float(v) for k,v in record.items()})
    pairs=[compare(work/('wave_bdf_'+a+'_waveform/trace.csv'),work/('wave_bdf_'+b+'_waveform/trace.csv')) for a,b in zip(grids,grids[1:])]
    if any(c['nodes']!=61 or c['interval_s'][0]!=0 or abs(c['interval_s'][1]-61e-9)>1e-18 for c in pairs):
        raise RuntimeError('full 61-node startup/acquisition/conversion record is incomplete')
    result=dict(status='PASS',scope='nominal 1.65 V connected conversion; all 61 nodes, startup, acquisition and complete conversion interval; not PVT/layout qualification',
                method='BDF2 with event restarts and local driver-edge refinement; unshifted waveforms',edge_steps_fs=[2.,1.,.5],edge_window_ps=5.,steps_ps=[float(s) for s in grids],solver_records=solver_records,limits=LIMITS,comparisons=pairs)
    if pairs[-1]['failed_gates']:result['status']='FAIL'
    if pairs[-1]['maximum_voltage_difference_V']>=.6*pairs[0]['maximum_voltage_difference_V']:
        result['status']='FAIL';result['refinement_failure']='maximum all-node error did not shrink by at least 40%'
    for step in grids:
        with (work/('wave_bdf_'+step+'.csv')).open() as f:row=next(csv.DictReader(f))
        if row['code']!='128' or row['pass']!='1':raise RuntimeError('refined electrical conversion invalid')
    (work/'checks.json').write_text(json.dumps(result,indent=2,allow_nan=False)+'\n')
    if result['status']!='PASS':raise RuntimeError('all-node waveform convergence failed; inspect checks.json')
    print('PASS all-node waveform convergence')

if __name__=='__main__':
    p=argparse.ArgumentParser(description=__doc__);p.add_argument('--bench',type=Path,required=True);p.add_argument('--work',type=Path,required=True)
    a=p.parse_args();run(a.bench.resolve(),a.work.resolve())
