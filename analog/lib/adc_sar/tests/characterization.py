#!/usr/bin/env python3
"""Measured single-slice characterization; stressed failures are retained."""
import argparse
import csv
import json
import math
from pathlib import Path
import subprocess
import sys
sys.path.insert(0,str(Path(__file__).resolve().parents[1]))
from metrics import spectrum

SUITES=('frequency','amplitude','impedance','kickback','psrr','acquisition','evaluation','jitter','power')

def rows(path):
    with path.open() as f:return list(csv.DictReader(f))

def sinusoid_fit(times,values,frequency):
    """DC + sin + cos least squares on the actual aperture timestamps."""
    if len(times)!=len(values) or len(times)<4 or not frequency>0:raise ValueError('insufficient sinusoid data')
    if any(not math.isfinite(v) for v in [*times,*values]):raise ValueError('nonfinite sinusoid data')
    basis=[(1.,math.sin(2*math.pi*frequency*t),math.cos(2*math.pi*frequency*t)) for t in times]
    m=[[sum(b[i]*b[j] for b in basis) for j in range(3)]+[sum(b[i]*v for b,v in zip(basis,values))] for i in range(3)]
    for i in range(3):
        k=max(range(i,3),key=lambda k:abs(m[k][i]));m[i],m[k]=m[k],m[i]
        if abs(m[i][i])<1e-10:raise ValueError('unresolved sinusoid fit')
        pivot=m[i][i];m[i]=[v/pivot for v in m[i]]
        for k in range(3):
            if k!=i:
                factor=m[k][i];m[k]=[a-factor*b for a,b in zip(m[k],m[i])]
    dc,sine,cosine=[m[i][3] for i in range(3)]
    residual=[v-dc-sine*b[1]-cosine*b[2] for v,b in zip(values,basis)]
    return dict(dc=dc,amplitude=math.hypot(sine,cosine),phase_rad=math.atan2(cosine,sine),
                residual_rms=math.sqrt(sum(v*v for v in residual)/len(residual)))

def finite_metrics(values):return {k:(v if math.isfinite(v) else None) for k,v in values.items()}

def characterize_case(work):
    config=rows(work/'configuration.csv')[0]
    c={k:(v if k=='name' else float(v)) for k,v in config.items()}
    native=rows(work/'conversion.csv'); detail=rows(work/'conversion_characterization.csv')
    edges=rows(work/'conversion_edges.csv');power=rows(work/'conversion_power.csv')
    if not native or len(native)!=len(detail):raise RuntimeError('incomplete electrical characterization')
    n=len(native);get=lambda key:[float(r[key]) for r in detail]
    for a,b in zip(native,detail):
        if a['sample']!=b['sample']:raise RuntimeError('lost sample identity')
    failed=sum(float(r['pass'])!=1 for r in detail)
    if c['required'] and failed:raise RuntimeError(f"{work}: {failed}/{n} required operating points fail")
    full=[r for r in power if r['phase']=='full_cycle'];startup=[r for r in power if r['phase']=='startup']
    if len(full)!=n or len(startup)!=1:raise RuntimeError('missing full-cycle/startup energy')
    for r in full:
        sample=r['sample'];parts=[p for p in power if p['sample']==sample and p['phase'] in ('track_capture','convert_recover')]
        if len(parts)!=2:raise RuntimeError('missing cycle power phase')
        for key in ('duration_s','io_energy_J','core_energy_J','io_charge_C','core_charge_C'):
            if not math.isclose(float(r[key]),sum(float(p[key]) for p in parts),rel_tol=1e-10,abs_tol=1e-24):
                raise RuntimeError('inconsistent cycle energy/charge accounting')
        if abs(float(r['duration_s'])-c['sample_period_s'])>1e-18:raise RuntimeError('incomplete cycle duration')
    result=dict(configuration=c,conversions=n,failed_operating_points=failed,required=bool(c['required']),
                maximum_external_acquisition_V=max(get('external_acquisition_V')),
                maximum_pin_acquisition_V=max(float(r['acquisition_V']) for r in native),
                maximum_hold_variation_V=max(float(r['hold_pp_V']) for r in native),
                maximum_external_code_error=max(get('external_code_error')),
                maximum_pin_code_error=max(float(r['code_error']) for r in native),
                invalid_decisions=sum(int(r['invalid_decisions']) for r in native),
                minimum_decision_margin_s=min(float(r['minimum_decision_margin_s']) for r in native),
                maximum_upstream_kickback_V=max(get('hold_kickback_peak_V')),
                maximum_abs_kickback_charge_C=max(map(abs,get('hold_kickback_charge_C'))),
                startup={k:float(v) for k,v in startup[0].items() if k not in ('phase','sample')},
                full_cycle_io_energy_J=get('io_energy_J'),full_cycle_core_energy_J=get('core_energy_J'),
                full_cycle_io_average_A=get('io_average_A'),full_cycle_core_average_A=get('core_average_A'),
                full_cycle_io_peak_A=get('io_peak_A'),full_cycle_core_peak_A=get('core_peak_A'))
    if c['frequency_Hz']:
        bin_=round(c['frequency_Hz']*c['sample_period_s']*n)
        if abs(bin_-c['frequency_Hz']*c['sample_period_s']*n)>1e-8:raise RuntimeError('noncoherent stimulus')
        result['spectrum']=finite_metrics(spectrum([float(r['code']) for r in native],bin_))
        t=get('aperture_s');ideal=get('ideal_input_V');pin=get('pin_input_V');held=get('held_V')
        ideal_fit=sinusoid_fit(t,ideal,c['frequency_Hz']);held_fit=sinusoid_fit(t,held,c['frequency_Hz'])
        result['input_transfer']=dict(ideal=ideal_fit,pin=sinusoid_fit(t,pin,c['frequency_Hz']),held=held_fit,
            held_gain=held_fit['amplitude']/ideal_fit['amplitude'],
            held_phase_rad=math.atan2(math.sin(held_fit['phase_rad']-ideal_fit['phase_rad']),math.cos(held_fit['phase_rad']-ideal_fit['phase_rad'])))
    if c['ripple_Hz']:
        t=get('aperture_s');rail='core_capture_V' if c['ripple_core_V'] else 'io_capture_V'
        ref=sinusoid_fit(t,get(rail),c['ripple_Hz']);err=sinusoid_fit(t,[a-b for a,b in zip(get('held_V'),get('ideal_input_V'))],c['ripple_Hz'])
        output=sinusoid_fit(t,[float(r['code'])*3.3/256 for r in native],c['ripple_Hz'])
        if ref['amplitude']<=1e-6:raise RuntimeError('ripple stimulus did not reach the selected rail')
        result['psrr']=dict(rail=rail,delivered_ripple_amplitude_V=ref['amplitude'],held_error_amplitude_V=err['amplitude'],
            sampled_hold_psrr_db=20*math.log10(ref['amplitude']/err['amplitude']) if err['amplitude']>=1e-6 else None,held_reporting_floor_V=1e-6,
            output_code_equivalent_amplitude_V=output['amplitude'],
            output_code_psrr_db=20*math.log10(ref['amplitude']/output['amplitude']) if output['amplitude']>=3.3/512 else None,
            output_resolution_floor_V=3.3/512)
    result['clock_delivery']={}
    for kind,count in (('aperture',n),('evaluate',8*n)):
        data=[r for r in edges if r['kind']==kind]
        if len(data)!=count:raise RuntimeError('missing physical clock edges')
        requested=[float(r['requested_offset_s']) for r in data];delivered=[float(r['delivered_offset_s']) for r in data]
        residual=[a-b for a,b in zip(delivered,requested)];mean=sum(residual)/count
        error=max(abs(v-mean) for v in residual)
        if error>5e-12:raise RuntimeError(f'physical clock jitter corrupted/quantized: {kind}, {error} s')
        rms=lambda a:math.sqrt(sum((v-sum(a)/len(a))**2 for v in a)/len(a))
        result['clock_delivery'][kind]=dict(edges=count,requested_rms_s=rms(requested),delivered_rms_s=rms(delivered),
                                          mean_driver_delay_s=mean,maximum_centered_delivery_error_s=error)
    return result

def summarize(work,suite):
    cases=[characterize_case(p.parent) for p in sorted(work.glob('*/configuration.csv'))]
    if not cases:raise RuntimeError('no characterization cases')
    if suite in ('acquisition','evaluation') and not any(c['failed_operating_points'] for c in cases):
        raise RuntimeError('short-timing negative controls failed to expose any failure')
    psrr_refinement=None
    if suite=='psrr':
        psrr_refinement={}
        for rail in ('io','core'):
            coarse=next(c for c in cases if c['configuration']['name']==rail+'_5')['psrr']
            fine=next(c for c in cases if c['configuration']['name']==rail+'_refined5ps')['psrr']
            delta=abs(coarse['held_error_amplitude_V']-fine['held_error_amplitude_V'])
            limit=max(1e-7,.12*max(coarse['held_error_amplitude_V'],fine['held_error_amplitude_V']))
            if delta>limit:raise RuntimeError('sampled hold PSRR amplitude is timestep-sensitive')
            psrr_refinement[rail]=dict(steps_ps=[10,5],held_response_amplitude_difference_V=delta,acceptance_V=limit)
    power_refinement=None
    if suite=='power':
        coarse=next(c for c in cases if c['configuration']['name']=='refined_0.312500')
        fine=next(c for c in cases if c['configuration']['name']=='refined_0.156250')
        keys=('full_cycle_io_energy_J','full_cycle_core_energy_J','full_cycle_io_average_A','full_cycle_core_average_A','full_cycle_io_peak_A','full_cycle_core_peak_A')
        differences={k:max(abs(a/b-1) for a,b in zip(coarse[k],fine[k])) for k in keys}
        if any(v>(.05 if 'peak' in k else .01) for k,v in differences.items()):raise RuntimeError('full-cycle power refinement failed')
        power_refinement=dict(steps_ps=[.3125,.15625],method='BDF2',relative_differences=differences,energy_charge_limit=.01,peak_limit=.05)
    result=dict(status='PASS',suite=suite,scope='generic connected single slice; finite characterization grid, not IHP corner/layout signoff',
                psrr_refinement=psrr_refinement,power_refinement=power_refinement,cases=cases,conversions=sum(c['conversions'] for c in cases),
                failed_operating_points=sum(c['failed_operating_points'] for c in cases),
                qualification='PASS means required regression points and measurement guards pass; stressed failures remain reported. Unspecified product budgets are not PASS.')
    (work/'checks.json').write_text(json.dumps(result,indent=2,allow_nan=False)+'\n');return result

if __name__=='__main__':
    p=argparse.ArgumentParser(description=__doc__);p.add_argument('--bench',type=Path,required=True)
    p.add_argument('--suite',choices=SUITES+('smoke',),required=True);p.add_argument('--work',type=Path,required=True)
    a=p.parse_args();a.work.mkdir(parents=True,exist_ok=True)
    subprocess.run([str(a.bench.resolve()),'characterize_'+a.suite,str(a.work.resolve())],check=True)
    result=summarize(a.work,a.suite);print('PASS',a.suite,result['conversions'],'conversions;',result['failed_operating_points'],'failed stressed operating points retained')
