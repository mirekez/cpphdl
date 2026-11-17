#!/usr/bin/env python3
"""Run one complete, content-addressed native process/corner campaign."""
import argparse,json,os,shlex,subprocess
from pathlib import Path
from provenance import digest,verify,receipt
from measure import measure

def run(binding,bench,work,step,degree,levels,build_receipt=None,timeout=7200):
    binding,bench,work=map(lambda p:p.resolve(),(binding,bench,work))
    if work.exists() and any(work.iterdir()):raise ValueError('use an empty campaign directory')
    files={Path(__file__).resolve(),Path(__file__).with_name('measure.py').resolve(),bench}
    files.update(p for p in binding.iterdir() if p.is_file())
    for p in binding.glob('model_*.txt'):
        binary=shlex.split(p.read_text().splitlines()[0])
        if len(binary)!=2 or binary[0]!='binary':raise ValueError('missing OSDI binary identity')
        files.add(Path(binary[1]).resolve())
    hashes={str(p):digest(p) for p in files}
    if build_receipt:
        compiled=json.loads(build_receipt.read_text());verify(compiled)
        if compiled.get(str(bench))!=digest(bench):raise ValueError('build receipt does not identify this executable')
        hashes.update(compiled)
    work.mkdir(parents=True,exist_ok=True)
    (work/'inputs.json').write_text(json.dumps(hashes,indent=2)+'\n')
    command=[str(bench),'--degree',str(degree),str(binding/'circuit.txt'),str(work),str(step),*map(str,levels)]
    result=dict(status='RUNNING',command=command,qualified=False)
    try:
        with (work/'native.log').open('w') as log:
            r=subprocess.run(command,stdout=log,stderr=-2,env=dict(os.environ,OMP_NUM_THREADS='1'),timeout=timeout)
        if r.returncode:raise RuntimeError('native simulation failed; see native.log')
        native=json.loads((work/'native.json').read_text())
        if not native['complete'] or not native['conversion_complete']:raise ValueError('incomplete native campaign')
        measured=measure(work)
        if [r['input_V'] for r in measured['samples']]!=levels:raise ValueError('conversion inventory differs from requested stimulus')
        result.update(status=measured['status'],native=native,measurements=measured)
        verify(hashes)
    except Exception as e:result.update(status='ERROR',error=f'{type(e).__name__}: {e}')
    (work/'checks.json').write_text(json.dumps(result,indent=2)+'\n')
    if result['status']!='ERROR':receipt(work/'manifest.json',hashes,[p for p in work.iterdir() if p.is_file() and p.name not in ('inputs.json','manifest.json')],status=result['status'],qualified=False)
    return result

if __name__=='__main__':
    p=argparse.ArgumentParser(description=__doc__)
    for key in ('binding','bench','work'):p.add_argument('--'+key,type=Path,required=True)
    p.add_argument('--build-receipt',type=Path);p.add_argument('--step',type=float,default=20e-12);p.add_argument('--degree',type=int,default=32)
    p.add_argument('--levels',type=float,nargs='+',default=[.85,1.15,1.45,1.75,2.05]);p.add_argument('--timeout',type=float,default=7200)
    a=p.parse_args();r=run(a.binding,a.bench,a.work,a.step,a.degree,a.levels,a.build_receipt,a.timeout)
    print(r['status']);raise SystemExit(r['status']!='PASS')
