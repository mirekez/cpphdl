"""Calibrate one-sided input-referred PSD and Gaussian ADC transition noise.

No PSD, temperature, bandwidth or measurement provenance is inferred from a
MOS model name. Transition fitting consumes actual codes and quiet input
voltages, never the injected random offsets.
"""
import argparse
import csv
import hashlib
import json
import math
from pathlib import Path


def fit_psd(frequencies, psd):
    """Relative-error NNLS fit of S(f)=white+K/f, PSD in V^2/Hz."""
    if len(frequencies)<4 or len(frequencies)!=len(psd):
        raise ValueError('need at least four matched PSD points')
    if any(not math.isfinite(x) or x<=0 for x in [*frequencies,*psd]):
        raise ValueError('frequency and one-sided PSD must be positive and finite')
    if any(b<=a for a,b in zip(frequencies,frequencies[1:])):
        raise ValueError('PSD frequencies must be strictly increasing')
    f0=frequencies[0]; scale=max(psd)
    x=[f0/f for f in frequencies]; y=[s/scale for s in psd]
    if min(y)<1e-150:raise ValueError('PSD dynamic range exceeds fitting precision')
    w=[1/s**2 for s in y]
    sw=sum(w); sx=sum(a*b for a,b in zip(w,x)); sxx=sum(a*b*b for a,b in zip(w,x))
    sy=sum(a*b for a,b in zip(w,y)); sxy=sum(a*b*c for a,b,c in zip(w,x,y))
    determinant=sw*sxx-sx*sx
    if determinant<=1e-12*sw*sxx: raise ValueError('PSD cannot separate white and flicker terms')
    candidates=[(sy/sw,0),(0,sxy/sxx)]
    a=(sy*sxx-sxy*sx)/determinant; b=(sw*sxy-sx*sy)/determinant
    if a>=0 and b>=0:candidates.append((a,b))
    a,b=min(candidates,key=lambda ab:sum(((ab[0]+ab[1]*xx)/yy-1)**2 for xx,yy in zip(x,y)))
    white=a*scale; flicker=b*scale*f0
    errors=[(white+flicker/f)/s-1 for f,s in zip(frequencies,psd)]
    return dict(white_psd_V2_Hz=white,flicker_V2=flicker,
                relative_rms_residual=math.sqrt(sum(e*e for e in errors)/len(errors)),
                maximum_relative_residual=max(map(abs,errors)),points=len(psd),
                measured_band_Hz=[frequencies[0],frequencies[-1]])


def _minimum(fn, low, high, iterations=70):
    ratio=(math.sqrt(5)-1)/2
    a=high-ratio*(high-low);b=low+ratio*(high-low);fa=fn(a);fb=fn(b)
    for _ in range(iterations):
        if fa<fb:
            high,b,fb=b,a,fa;a=high-ratio*(high-low);fa=fn(a)
        else:
            low,a,fa=a,b,fb;b=low+ratio*(high-low);fb=fn(b)
    return (a,fa) if fa<fb else (b,fb)


def fit_transition_noise(levels, high_counts, counts):
    """Binomial probit MLE; 95% profile-likelihood CI assumes IID Gaussian noise.

    Fit both the transition center and sigma. A single code-center histogram
    cannot measure sub-LSB noise. Reject an unresolved/all-deterministic scan.
    """
    if len(levels)<3 or len(levels)!=len(high_counts) or len(levels)!=len(counts):
        raise ValueError('need three or more matched transition probability levels')
    if any(not math.isfinite(v) for v in levels) or any(b<=a for a,b in zip(levels,levels[1:])):
        raise ValueError('transition voltages must be finite and increasing')
    if any(type(n) is not int or type(h) is not int or n<=0 or not 0<=h<=n for h,n in zip(high_counts,counts)):
        raise ValueError('invalid binomial counts')
    if sum(0<h<n for h,n in zip(high_counts,counts))<2:
        raise ValueError('noise width unresolved: need mixed outcomes at two or more input levels')
    origin=(levels[0]+levels[-1])/2; span=levels[-1]-levels[0]
    x=[(v-origin)/span for v in levels]
    def nll(mu,log_sigma):
        sigma=math.exp(log_sigma)
        result=0
        for v,h,n in zip(x,high_counts,counts):
            prob=.5*math.erfc(-(v-mu)/(sigma*math.sqrt(2)))
            prob=max(1e-15,min(1-1e-15,prob))
            result-=h*math.log(prob)+(n-h)*math.log1p(-prob)
        return result
    def profile(log_sigma):
        return _minimum(lambda mu:nll(mu,log_sigma),-4.,4.)
    grid=[-10+i*13/52 for i in range(53)]
    best=min(range(len(grid)),key=lambda i:profile(grid[i])[1])
    if best in (0,len(grid)-1):raise ValueError('noise fit reached its identifiability boundary')
    log_sigma,minimum=_minimum(lambda z:profile(z)[1],grid[best-1],grid[best+1])
    mu,_=profile(log_sigma)
    target=minimum+1.920729410347062 # 0.5*chi-square_1(0.95), asymptotic profile interval.
    def bound(left,right,increasing):
        for _ in range(60):
            mid=(left+right)/2
            if (profile(mid)[1]>target)==increasing:right=mid
            else:left=mid
        return math.exp((left+right)/2)*span
    if profile(-10)[1]<=target or profile(3)[1]<=target:
        raise ValueError('noise confidence interval is unbounded within the search range')
    interval=[bound(-10,log_sigma,False),bound(log_sigma,3,True)]
    sigma=math.exp(log_sigma)*span
    predictions=[.5*math.erfc(-(v-mu)/(math.exp(log_sigma)*math.sqrt(2))) for v in x]
    return dict(center_V=origin+mu*span,sigma_V=sigma,sigma_95_interval_V=interval,
                negative_log_likelihood=minimum,probabilities=predictions,
                observations=sum(counts),method='binomial Gaussian probit; IID assumption; 95% profile likelihood')


def calibrate(path, source_kind, reference, condition, sampling_included, low, high,
              temperature_K,maximum_residual=.1):
    if source_kind not in ('synthetic','compact-model','measurement') or not reference.strip() or not condition.strip():
        raise ValueError('explicit source kind, reference and operating condition required')
    if not math.isfinite(temperature_K) or temperature_K<=0:raise ValueError('positive calibration temperature required')
    with path.open() as source:rows=list(csv.DictReader(source))
    if not rows or set(rows[0])!={'frequency_Hz','input_psd_V2_Hz'}:
        raise ValueError('expected frequency_Hz,input_psd_V2_Hz CSV columns (linear one-sided PSD)')
    fit=fit_psd([float(r['frequency_Hz']) for r in rows],[float(r['input_psd_V2_Hz']) for r in rows])
    if not math.isfinite(maximum_residual) or maximum_residual<=0 or fit['relative_rms_residual']>maximum_residual:
        raise ValueError('white plus flicker model fails PSD residual acceptance')
    if not (fit['measured_band_Hz'][0]<=low<high<=fit['measured_band_Hz'][1]):
        raise ValueError('integration band must be inside measured PSD coverage')
    return dict(schema=1,source_kind=source_kind,reference=reference,condition=condition,
                input_sha256=hashlib.sha256(path.read_bytes()).hexdigest(),input_file=str(path.resolve()),
                sampling_noise_included=bool(sampling_included),fit=fit,maximum_relative_rms_residual=maximum_residual,
                profile=dict(include_ktc=not sampling_included,capacitance_F=0,ktc_variance_scale=1,
                             white_psd_V2_Hz=fit['white_psd_V2_Hz'],flicker_V2=fit['flicker_V2'],low_Hz=low,high_Hz=high,
                             reference_temperature_K=temperature_K),
                integrated_psd_variance_V2=fit['white_psd_V2_Hz']*(high-low)+fit['flicker_V2']*math.log(high/low),
                qualification='coefficient fit only; residual acceptance and source validity must be reviewed')


def main():
    p=argparse.ArgumentParser(description=__doc__)
    p.add_argument('--psd',type=Path,required=True);p.add_argument('--output',type=Path,required=True)
    p.add_argument('--header',type=Path)
    p.add_argument('--source-kind',choices=['synthetic','compact-model','measurement'],required=True)
    p.add_argument('--reference',required=True);p.add_argument('--condition',required=True)
    p.add_argument('--sampling-noise',choices=['included','excluded'],required=True)
    p.add_argument('--low-hz',type=float,required=True);p.add_argument('--high-hz',type=float,required=True)
    p.add_argument('--temperature-k',type=float,required=True)
    p.add_argument('--maximum-relative-rms-residual',type=float,default=.1)
    a=p.parse_args()
    r=calibrate(a.psd,a.source_kind,a.reference,a.condition,a.sampling_noise=='included',a.low_hz,a.high_hz,
                a.temperature_k,a.maximum_relative_rms_residual)
    a.output.write_text(json.dumps(r,indent=2,allow_nan=False)+'\n')
    if a.header:
        profile=r['profile']
        text=['#pragma once','#include <lib/adc_sar/noise.h>',
              '// Input PSD SHA256: '+r['input_sha256'],
              '// Provenance and operating conditions are recorded in the calibration JSON.',
              'inline constexpr analog::adc_sar::noise_parameters calibrated_noise{']
        text += ['    .'+k+' = '+(str(v).lower() if isinstance(v,bool) else format(v,'.17g'))+',' for k,v in profile.items()]
        text += ['};','']
        a.header.write_text('\n'.join(text))


if __name__=='__main__':main()
