#!/usr/bin/env python3
"""Blind calibration of sampled equivalent noise through ONE electrical ADC."""
import argparse
from concurrent.futures import ThreadPoolExecutor
import csv
import json
import math
from pathlib import Path
import random
import subprocess
import sys
sys.path.insert(0,str(Path(__file__).resolve().parents[1]))
from noise_calibration import fit_transition_noise,calibrate
from transitions import ElectricalADC,LOW,HIGH


def read_csv(path):
    with path.open() as f:return list(csv.DictReader(f))


def calibration_fixture(work,noise_bench):
    """Known independent PSD fixture: validates the fitter, not IHP noise."""
    path=work/'synthetic_psd.csv'
    with path.open('w',newline='') as f:
        w=csv.writer(f);w.writerow(['frequency_Hz','input_psd_V2_Hz'])
        for i in range(33):
            frequency=10**(3+i/8);w.writerow([frequency,4e-16+4e-10/frequency])
    r=calibrate(path,'synthetic','Analytic S(f)=4e-16+4e-10/f V^2/Hz; regression fixture',
                '27 C, input-referred; additional PSD excludes sampled kT/C',False,1e3,8e6,300.15)
    if abs(r['fit']['white_psd_V2_Hz']/4e-16-1)>1e-9 or abs(r['fit']['flicker_V2']/4e-10-1)>1e-9:
        raise RuntimeError('PSD calibration failed independent coefficient recovery')
    (work/'calibrated_profile.json').write_text(json.dumps(r,indent=2,allow_nan=False)+'\n')
    # Exercise the public CSV -> JSON/header command, then feed its coefficients
    # back through the C++ slice factory and native noise generator.
    subprocess.run([sys.executable,str(Path(__file__).resolve().parents[1]/'noise_calibration.py'),
        '--psd',str(path),'--output',str(work/'calibrated_profile.json'),'--header',str(work/'calibrated_noise.h'),
        '--source-kind','synthetic','--reference',r['reference'],'--condition',r['condition'],
        '--sampling-noise','excluded','--low-hz','1000','--high-hz','8000000','--temperature-k','300.15'],check=True)
    r=json.loads((work/'calibrated_profile.json').read_text());p=r['profile']
    (work/'calibration_input.txt').write_text(' '.join(str(v) for v in
        (int(p['include_ktc']),p['white_psd_V2_Hz'],p['flicker_V2'],p['low_Hz'],p['high_Hz'],p['reference_temperature_K']))+'\n')
    subprocess.run([str(noise_bench),'profile',str(work)],check=True)
    r['native_validation']=read_csv(work/'calibrated_noise_statistics.csv')[0]
    return r


def characterize(bench,work,sources,refined=False,smoke=False):
    adc=ElectricalADC(bench,work,refined=refined)
    sigma=float(sources[0]['expected_sigma_V']);tolerance=sigma/32
    observations=[];reference_checks=0;seed_fits={}
    out=(work/'noise_observations.csv').open('w',newline='')
    writer=csv.DictWriter(out,['sample','seed','noise_index','quiet_input_V','injected_noise_V',
                              'actual_adc_input_V','code','threshold_code','invalid_decisions'])
    writer.writeheader()
    try:
        # Fresh local numerical threshold; do not borrow a 0.2 mV DNL bracket
        # to claim a measurement of noise whose RMS is only 0.064 mV.
        lo=adc.query(1.638,'noise_zero_bracket','up',128)
        hi=adc.query(1.658,'noise_zero_bracket','up',128)
        if not lo['code']<128<=hi['code']:raise RuntimeError('no noiseless T[128] bracket')
        while hi['requested_V']-lo['requested_V']>tolerance:
            row=adc.query((lo['requested_V']+hi['requested_V'])/2,'noise_zero_bisect','up',128)
            if row['code']<128:lo=row
            else:hi=row
        center=(lo['requested_V']+hi['requested_V'])/2
        def zero_controls():
            nonlocal reference_checks
            for sign,expected in [(-1,127),(1,128)]:
                for _ in range(4):
                    row=adc.query(center+sign*sigma/4,'noise_zero_repeat','up',128)
                    if row['code']!=expected:
                        raise RuntimeError('noiseless numerical/history variation obscures sub-LSB noise')
                    reference_checks+=1
        zero_controls()
        seeds=[7] if refined or smoke else [7,19,31,43]
        repetitions=4 if smoke else 32
        offsets=(-2,-1,0,1,2)
        for seed in seeds:
            noise=[float(r['noise_V']) for r in sources if int(r['seed'])==seed]
            levels=list(offsets)*repetitions
            random.Random(123+seed).shuffle(levels) # Prevent monotonic drift from masquerading as width.
            for index,multiple in enumerate(levels):
                quiet=center+multiple*sigma;stimulus=quiet+noise[index]
                if not LOW<=stimulus<=HIGH:raise RuntimeError('noise stimulus clipped at ADC input limit')
                row=adc.query(stimulus,'noise_histogram',str(seed),128)
                if row['code'] not in (127,128):raise RuntimeError('noise response spans unexpected codes')
                record=dict(sample=int(row['sample']),seed=seed,noise_index=index,quiet_input_V=quiet,
                            injected_noise_V=noise[index],actual_adc_input_V=row['input_V'],code=int(row['code']),
                            threshold_code=128,invalid_decisions=int(row['invalid_decisions']))
                writer.writerow(record);out.flush();observations.append(record)
                if (index+1)%40==0:print(f'{"5" if refined else "10"} ps seed {seed}: {index+1}/{len(levels)} noisy conversions',flush=True)
            if not smoke:seed_fits[str(seed)]=fit_observations([r for r in observations if r['seed']==seed])
        zero_controls()
        result=dict(status='PASS',step_s=5e-12 if refined else 10e-12,expected_sigma_V=sigma,
                    threshold_bracket_V=[lo['requested_V'],hi['requested_V']],threshold_bracket_limit_V=tolerance,
                    noise_off_checks=reference_checks,noisy_conversions=len(observations),
                    total_conversions=adc.count,seed_fits=seed_fits,smoke=smoke)
        if not smoke:
            result['fit']=fit_observations(observations)
            result['sigma_relative_error']=result['fit']['sigma_V']/sigma-1
            if abs(result['sigma_relative_error'])>.25:
                raise RuntimeError('blind ADC noise calibration differs from injected RMS by more than 25%')
        adc.close();out.close()
    except BaseException:
        adc.close(abort=True);out.close();raise
    (work/'checks.json').write_text(json.dumps(result,indent=2,allow_nan=False)+'\n')
    return result


def fit_observations(observations):
    # The estimator never receives injected_noise_V or the known sigma.
    levels=sorted({r['quiet_input_V'] for r in observations})
    counts=[sum(r['quiet_input_V']==v for r in observations) for v in levels]
    high=[sum(r['quiet_input_V']==v and r['code']>=128 for r in observations) for v in levels]
    return dict(fit_transition_noise(levels,high,counts),levels_V=levels,high_counts=high,counts=counts)


def run(bench,noise_bench,work,smoke=False):
    work.mkdir(parents=True,exist_ok=True)
    # Own fixtures make this test runnable alone; no hidden dependency on a
    # previous CTest run, a cached transition campaign or files in the source tree.
    subprocess.run([str(noise_bench),'model',str(work/'sources')],check=True)
    sources=read_csv(work/'sources/noise_samples.csv')
    profile=calibration_fixture(work,noise_bench)
    if smoke:
        coarse=characterize(bench,work/'coarse',sources,smoke=True)
    else:
        # Independent timestep runs have separate electrical states and output
        # directories. Each retains one live slice and its full sample history.
        with ThreadPoolExecutor(max_workers=2) as executor:
            coarse_job=executor.submit(characterize,bench,work/'coarse',sources)
            fine_job=executor.submit(characterize,bench,work/'refined',sources,True)
            coarse=coarse_job.result();fine=fine_job.result()
    result=dict(status='PASS',scope='sampled input-equivalent kT/C calibration through one generic electrical ADC',
                psd_calibration=profile,coarse=coarse,smoke=smoke,
                limitations=['white/flicker PSD fixture is synthetic, not IHP calibration',
                             'does not include intrinsic PSP transient noise or stochastic comparator tails',
                             'no product RMS-noise or noise-free-resolution acceptance budget specified'])
    if not smoke:
        difference=fine['fit']['sigma_V']/coarse['seed_fits']['7']['sigma_V']-1
        result.update(refined=fine,paired_seed_timestep_sigma_difference_fraction=difference)
        if abs(difference)>.15:raise RuntimeError('noise-width timestep change exceeds 15% on paired random samples')
    (work/'checks.json').write_text(json.dumps(result,indent=2,allow_nan=False)+'\n')
    return result


if __name__=='__main__':
    p=argparse.ArgumentParser(description=__doc__)
    p.add_argument('--bench',type=Path,required=True);p.add_argument('--noise-bench',type=Path,required=True)
    p.add_argument('--work',type=Path,required=True);p.add_argument('--smoke',action='store_true')
    a=p.parse_args()
    try:
        r=run(a.bench.resolve(),a.noise_bench.resolve(),a.work.resolve(),a.smoke)
        print(json.dumps(r,indent=2))
    except Exception as error:
        print(str(error),file=sys.stderr);raise SystemExit(1)
