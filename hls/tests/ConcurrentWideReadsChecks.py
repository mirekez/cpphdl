#!/usr/bin/env python3
"""Check paired 512-bit typed loads, high lanes, and retimed overlap."""
import argparse
import hashlib
import json
import os
from pathlib import Path
import subprocess


def main():
    p=argparse.ArgumentParser()
    for name in ('cpphdl','cxx','verilator','work'):
        p.add_argument('--'+name,type=Path,required=True)
    a=p.parse_args()
    for name in ('cpphdl','cxx','verilator','work'):
        setattr(a,name,getattr(a,name).resolve())
    tests=Path(__file__).resolve().parent
    root=tests.parents[1]
    source=tests/'ConcurrentWideReads.cpp'
    fixture=tests/'ConcurrentWideReadsRun.cpp'
    a.work.mkdir(parents=True,exist_ok=True)
    report={'status':'running','commands':[], 'results':{},
            'source_sha256':{str(f):hashlib.sha256(f.read_bytes()).hexdigest()
            for f in (source,fixture,root/'hls/delayed_scheduler.cpp',
                      root/'hls/Clocked.h',root/'hls/ExternalMemory.h',
                      root/'synth/retiming.cpp')}}
    def save():
        (a.work/'results.json').write_text(json.dumps(report,indent=2)+'\n')
    def fresh(name):
        path=a.work/name
        if path.exists():
            for number in range(1,1000):
                older=a.work/f'{name}-previous-{number}'
                if not older.exists():
                    path.rename(older)
                    break
        path.mkdir()
        return path
    def run(cmd,log):
        cmd=[str(x) for x in cmd];report['commands'].append(cmd);save()
        env=os.environ.copy()
        env['PATH']=str(a.verilator.parent)+os.pathsep+env.get('PATH','')
        env['LD_LIBRARY_PATH']=str(root/'.conda/lib')+os.pathsep+env.get('LD_LIBRARY_PATH','')
        with log.open('w') as out:
            proc=subprocess.run(cmd,stdout=out,stderr=subprocess.STDOUT,env=env,timeout=900)
        result=log.read_text()
        if proc.returncode:raise RuntimeError(f'{log}: {result[-3000:]}')
        return result
    try:
        native=a.work/'native'
        run([a.cxx,'-std=c++17','-O1','-I'+str(root/'include'),source,'-o',native],
            a.work/'native-build.log')
        if 'PASS native' not in run([native],a.work/'native-run.log'):
            raise AssertionError('native marker absent')
        raw=fresh('raw')
        run([a.cpphdl,'--generated-dir',raw,source,'--','-std=c++17','-DSYNTHESIS',
             '-I'+str(root/'include')],a.work/'raw-generate.log')
        sv=list(raw.glob('cpphdl_hls_ClockedMemory*.sv'))
        if len(sv)!=1 or not sv[0].stem.endswith('_D512_R2'):
            raise AssertionError(f'wrong 512-bit paired module: {sv}')
        mapped=fresh('mapped')
        run([a.cpphdl,'--synth','--top','worker','--module','ConcurrentWideTop',
             '--cxx',a.cxx,'--output',mapped,'--retiming','fit_pipeline_retiming',
             '--clock-period-ns','4','--tool-timeout','1200',source,'--','-std=c++17',
             '-DSYNTHESIS','-I'+str(root/'include')],a.work/'synth.log')
        timing=json.loads((mapped/'timing.json').read_text())
        if not timing['rules'][0]['target_met']:
            raise AssertionError('estimated retiming target absent')
        report['timing_rule']=timing['rules'][0]
        for kind,rtl,top in (('raw',sv[0],sv[0].stem),
                             ('retimed',mapped/'operations.v','ConcurrentWideTop')):
            obj=fresh(kind+'-obj')
            flags='-std=c++17 -O1'
            if kind=='retimed':flags+=' -DWIDE_READ_RETIMED'
            build=run([a.verilator,'--cc','--exe','--build','-j','2','-Wno-fatal',
                '--top-module',top,'--prefix','VConcurrentWideTop','--Mdir',obj,
                '-CFLAGS',flags,'-MAKEFLAGS',f'CXX={a.cxx} LINK={a.cxx}',rtl,fixture],
                a.work/(kind+'-build.log'))
            if '%Warning-LATCH' in build:raise AssertionError(kind+' latch warning')
            result=run([obj/'VConcurrentWideTop'],a.work/(kind+'-run.log'))
            if 'PASS concurrent wide reads overlap=1 reads=2 replies=2' not in result:
                raise AssertionError(kind+' paired overlap/result marker absent')
            report['results'][kind]=result.strip();save()
        report['status']='passed';save()
    except BaseException as e:
        report['status']='failed';report['error']=str(e);save();raise


if __name__=='__main__':main()
