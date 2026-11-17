#!/usr/bin/env python3
"""Closed-loop, one-slice ADC transition search; no ideal ADC supplies decisions."""
import argparse
import csv
import json
import math
import subprocess
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))
from metrics import transition_linearity

LOW, HIGH, REFERENCE, BITS = .85, 2.05, 3.3, 8
LSB = REFERENCE / (1 << BITS)
TOLERANCE = LSB / 64
FIELDS = 'sample,input_V,reference_V,acquisition_V,hold_pp_V,absolute_hold_V,code,ideal,code_error,decisions,invalid_decisions,margin_s,pass'.split(',')


class ElectricalADC:
    """One persistent native circuit for all commands in this context."""
    def __init__(self, bench, work, refined=False):
        work.mkdir(parents=True, exist_ok=True)
        self.log = (work/'server.log').open('w')
        self.file = (work/'queries.csv').open('w', newline='')
        self.writer = csv.DictWriter(self.file, ['phase','direction','transition','requested_V',*FIELDS])
        self.writer.writeheader()
        command = 'transition_server_refined' if refined else 'transition_server'
        self.process = subprocess.Popen([str(bench),command,str(work)], stdin=subprocess.PIPE,
                                        stdout=subprocess.PIPE,stderr=self.log,text=True,bufsize=1)
        if self.process.stdout.readline().strip().split(',') != FIELDS:
            self.close(abort=True)
            raise RuntimeError('invalid ADC server protocol')
        self.count = 0
        self.history = []

    def query(self, value, phase, direction, transition):
        if not math.isfinite(value) or not LOW <= value <= HIGH:
            raise ValueError('transition query outside declared operating range')
        self.process.stdin.write(format(value,'.17g')+'\n'); self.process.stdin.flush()
        line = self.process.stdout.readline()
        if not line:
            raise RuntimeError('electrical ADC stopped; inspect server.log')
        values = [float(x) for x in line.strip().split(',')]
        if len(values) != len(FIELDS) or any(not math.isfinite(x) for x in values):
            raise RuntimeError('invalid ADC response')
        row = dict(zip(FIELDS,values))
        row.update(phase=phase,direction=direction,transition=transition,requested_V=value)
        self.writer.writerow(row); self.file.flush()
        if row['sample'] != self.count:
            raise RuntimeError('slice was restarted or conversion sequence was lost')
        self.count += 1
        self.history.append(row)
        if abs(row['input_V']-value) > TOLERANCE/100:
            raise RuntimeError('commanded input did not reach the capture aperture')
        if row['decisions'] != BITS or row['invalid_decisions'] or not row['pass']:
            raise RuntimeError(f'unqualified electrical conversion at {value:.12g} V: {row}')
        if row['code'] != int(row['code']) or not 0 <= row['code'] < 1 << BITS:
            raise RuntimeError('invalid ADC output code')
        return row

    def close(self, abort=False):
        if getattr(self,'process',None):
            if abort and self.process.poll() is None:
                self.process.terminate()
            elif self.process.stdin and not self.process.stdin.closed:
                self.process.stdin.close()
            try:
                trailer = self.process.stdout.read()
                status = self.process.wait(timeout=30)
            except BaseException:
                self.process.kill(); self.process.wait(); raise
            finally:
                self.log.close(); self.file.close()
            if not abort and (status or not trailer.startswith('PASS transition_server')):
                raise RuntimeError(f'ADC server did not finish cleanly ({status}): {trailer}')
            self.process = None


def locate(adc, code, direction, endpoints, previous=None):
    """Bracket T[code] using actual valid codes; predict only a starting stimulus."""
    low, high = endpoints
    guess=code*LSB
    if previous is not None and abs(previous['code']-code)==1:
        guess=(previous['low']['requested_V']+previous['high']['requested_V'])/2+(code-previous['code'])*LSB
    # Predict a narrow interval for efficiency, but verify BOTH ends electrically.
    # Expand without assuming a local slope or the presence of any skipped code.
    radius=TOLERANCE
    left=max(LOW,min(HIGH,guess-radius)); right=max(LOW,min(HIGH,guess+radius))
    low=adc.query(left,'bracket',direction,code)
    high=adc.query(right,'bracket',direction,code)
    while low['code']>=code and left>LOW:
        radius*=2; left=max(LOW,guess-radius)
        low=adc.query(left,'expand',direction,code)
    while high['code']<code and right<HIGH:
        radius*=2; right=min(HIGH,guess+radius)
        high=adc.query(right,'expand',direction,code)
    if not low['code'] < code <= high['code']:
        raise RuntimeError(f'no electrical bracket for transition {code}')
    for _ in range(32):
        if high['requested_V']-low['requested_V'] <= TOLERANCE*(1+1e-12):
            return dict(code=code, direction=direction, low=low, high=high)
        probe = adc.query((low['requested_V']+high['requested_V'])/2,'bisect',direction,code)
        if probe['code'] < code: low = probe
        else: high = probe
    raise RuntimeError('transition search did not reach its voltage tolerance')


def verify_interiors(adc, boundaries, direction):
    """Check every complete code interval twice, retaining all circuit state."""
    ordered = sorted(boundaries,key=lambda b:b['code'])
    pairs = list(zip(ordered,ordered[1:]))
    if direction == 'down': pairs.reverse()
    for lower, upper in pairs:
        if upper['code'] != lower['code']+1:
            continue  # Only the explicitly selected smoke-test transitions are sparse.
        code = lower['code']
        left, right = lower['high']['requested_V'],upper['low']['requested_V']
        if right <= left:
            raise RuntimeError(f'code {code} has no proven nonzero interval (missing/narrow code)')
        center = (left+right)/2
        for _ in range(2):
            row = adc.query(center,'repeat_center',direction,code)
            if row['code'] != code:
                raise RuntimeError(f'code {code} interval/repeatability failure: observed {row["code"]}')


def write_boundaries(path, boundaries):
    with path.open('w',newline='') as file:
        writer=csv.writer(file)
        writer.writerow(['direction','code','low_V','high_V','transition_V','width_uncertainty_V',
                         'low_code','high_code','low_sample','high_sample'])
        for b in boundaries:
            lo,hi=b['low'],b['high']; a,z=lo['requested_V'],hi['requested_V']
            writer.writerow([b['direction'],b['code'],a,z,(a+z)/2,z-a,lo['code'],hi['code'],lo['sample'],hi['sample']])


def refinement(bench, work, coarse):
    """Independent one-slice run at half the timestep, around selected boundaries."""
    adc=ElectricalADC(bench,work/'refined',refined=True)
    found=[]
    try:
        for b in coarse:
            code=b['code']
            lo=adc.query(max(LOW,b['low']['requested_V']-2*TOLERANCE),'refinement','up',code)
            hi=adc.query(min(HIGH,b['high']['requested_V']+2*TOLERANCE),'refinement','up',code)
            if not lo['code'] < code <= hi['code']:
                raise RuntimeError(f'timestep refinement moved T[{code}] outside its 2-tolerance guard')
            # Use the guarded coarse interval, preserving fine-run circuit history.
            while hi['requested_V']-lo['requested_V'] > TOLERANCE:
                row=adc.query((lo['requested_V']+hi['requested_V'])/2,'refinement','up',code)
                if row['code'] < code: lo=row
                else: hi=row
            found.append(dict(code=code,direction='refined',low=lo,high=hi))
        count=adc.count; adc.close()
    except BaseException:
        adc.close(abort=True); raise
    write_boundaries(work/'refined_boundaries.csv',found)
    differences=[abs((a['low']['requested_V']+a['high']['requested_V']
                     -b['low']['requested_V']-b['high']['requested_V'])/2) for a,b in zip(coarse,found)]
    if max(differences)>2*TOLERANCE:
        raise RuntimeError('transition timestep convergence exceeds 2 search tolerances')
    return dict(codes=[b['code'] for b in found],coarse_step_s=10e-12,refined_step_s=5e-12,
                conversions=count,maximum_midpoint_difference_V=max(differences),
                acceptance_V=2*TOLERANCE)


def run(bench, work, smoke=False):
    work.mkdir(parents=True,exist_ok=True)
    adc = ElectricalADC(bench,work/'native')
    boundaries=[]
    try:
        low=adc.query(LOW,'endpoint','up',-1); high=adc.query(HIGH,'endpoint','up',-1)
        codes=list(range(int(low['code'])+1,int(high['code'])+1))
        if not codes: raise RuntimeError('ADC has no transitions across the declared range')
        if smoke: codes=[k for k in codes if k in (66,67,96,128,159)]
        for direction,sequence in [('up',codes),('down',list(reversed(codes)))]:
            # Re-observe endpoints in this direction on the same live slice.
            if direction=='down':
                high=adc.query(HIGH,'endpoint',direction,-1); low=adc.query(LOW,'endpoint',direction,-1)
                if list(range(int(low['code'])+1,int(high['code'])+1)) != list(range(codes[0],codes[-1]+1)):
                    raise RuntimeError('range endpoint codes changed with conversion history')
            previous=None; found=[]
            for code in sequence:
                previous=locate(adc,code,direction,(low,high),previous)
                found.append(previous);boundaries.append(previous)
                write_boundaries(work/'boundaries.csv',boundaries)
                print(f'{direction} T[{code}] bracket [{previous["low"]["requested_V"]:.9f}, '
                      f'{previous["high"]["requested_V"]:.9f}] V; {adc.count} conversions',flush=True)
            verify_interiors(adc,found,direction)
        count=adc.count
        adc.close()
    except BaseException:
        adc.close(abort=True)
        raise
    result=dict(status='PASS',scope='one persistent generic electrical ADC slice, nominal conditions',
                bits=BITS,reference_V=REFERENCE,input_range_V=[LOW,HIGH],tolerance_V=TOLERANCE,
                search_conversions=count,smoke=smoke)
    if not smoke:
        for direction in ('up','down'):
            selected=sorted((b for b in boundaries if b['direction']==direction),key=lambda b:b['code'])
            result[direction]=transition_linearity([b['code'] for b in selected],
                [b['low']['requested_V'] for b in selected],[b['high']['requested_V'] for b in selected],LSB)
        up={b['code']:b for b in boundaries if b['direction']=='up'}
        down={b['code']:b for b in boundaries if b['direction']=='down'}
        result['maximum_direction_difference_V']=max(abs((up[k]['low']['requested_V']+up[k]['high']['requested_V']
            -down[k]['low']['requested_V']-down[k]['high']['requested_V'])/2) for k in up)
        result['direction_acceptance_V']=2*TOLERANCE
        if result['maximum_direction_difference_V']>result['direction_acceptance_V']:
            raise RuntimeError('ascending/descending transition searches disagree beyond 2 search tolerances')
        result['repeat_center_conversions']=4*(len(codes)-1)
        result['refinement']=refinement(bench,work,[up[k] for k in (codes[0],96,128,codes[-1]) if k in up])
        result['dnl_inl_acceptance']='measured; product magnitude limits are unspecified'
    (work/'checks.json').write_text(json.dumps(result,indent=2,allow_nan=False)+'\n')
    return result


if __name__=='__main__':
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--bench',type=Path,required=True)
    parser.add_argument('--work',type=Path,required=True)
    parser.add_argument('--smoke',action='store_true')
    args=parser.parse_args()
    try:
        print(json.dumps(run(args.bench.resolve(),args.work.resolve(),args.smoke),indent=2))
    except Exception as error:
        print(str(error),file=sys.stderr)
        raise SystemExit(1)
