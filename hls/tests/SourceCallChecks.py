#!/usr/bin/env python3
"""Ordinary OpenMP call sites must retain concurrent, compiler-generated lanes."""
import argparse
import hashlib
import json
import os
from pathlib import Path
import re
import subprocess


def main():
    p=argparse.ArgumentParser(description=__doc__)
    for n in ('cpphdl','cxx','verilator','work'):
        p.add_argument('--'+n,type=Path,required=True)
    a=p.parse_args()
    for n in ('cpphdl','cxx','verilator','work'):setattr(a,n,getattr(a,n).resolve())
    tests=Path(__file__).resolve().parent;root=tests.parents[1]
    a.work.mkdir(parents=True,exist_ok=True)
    env=os.environ.copy()
    env['PATH']=str(a.verilator.parent)+os.pathsep+str(a.cxx.parent)+os.pathsep+env.get('PATH','')
    env['LD_LIBRARY_PATH']=str(root/'.conda/lib')+os.pathsep+env.get('LD_LIBRARY_PATH','')
    report={'status':'running','commands':[],'cases':[],
        'inputs':{str(f):hashlib.sha256(f.read_bytes()).hexdigest() for f in
                      (a.cpphdl,tests/'SourceCall.cpp',tests/'SourceCallRun.cpp',tests/'SourceCallStreamRun.cpp',
                       root/'hls/delayed_scheduler.cpp',root/'synth/StreamPipeline.h',
                       root/'synth/retiming.cpp')}}
    def save():(a.work/'results.json').write_text(json.dumps(report,indent=2)+'\n')
    def run(cmd,log):
        report['commands'].append(list(map(str,cmd)));save()
        with log.open('w') as stream:
            r=subprocess.run(list(map(str,cmd)),env=env,stdout=stream,stderr=subprocess.STDOUT,timeout=1200)
        text=log.read_text()
        if r.returncode:raise RuntimeError(f'{log}: {text[-4000:]}')
        return text
    try:
        for lanes in (2,4):
            work=a.work/f'lanes-{lanes}'
            if work.exists():
                for i in range(1,1000):
                    previous=a.work/f'lanes-{lanes}-previous-{i}'
                    if not previous.exists():work.rename(previous);break
            work.mkdir()
            defs=['-std=c++17','-fopenmp','-ffp-contract=off',f'-DSOURCE_CALL_LANES={lanes}',
                  '-I'+str(root),'-I'+str(root/'include')]
            run([a.cxx,*defs,tests/'SourceCall.cpp','-o',work/'native'],work/'native-build.log')
            run([work/'native'],work/'native.log')
            run([a.cpphdl,'--generated-dir',work/'raw',tests/'SourceCall.cpp','--',*defs,'-DSYNTHESIS'],work/'raw.log')
            sv=list((work/'raw').glob('cpphdl_hls_ClockedMemory*.sv'))
            if len(sv)!=1:raise AssertionError('missing ordinary generated module')
            raw=sv[0].read_text()
            if raw.count('\\MultiplyAddReplacement #(')!=2*lanes:
                raise AssertionError('OpenMP lanes/call sites lost from generated RTL')
            run([a.cpphdl,'--synth','--top','worker','--module','SourceCallTop','--cxx',a.cxx,
                 '--output',work/'mapped','--retiming','fit_pipeline_retiming','--clock-period-ns','4',
                 tests/'SourceCall.cpp','--',*defs,'-DSYNTHESIS'],work/'synth.log')
            timing=json.loads((work/'mapped/timing.json').read_text())
            replacements=timing['pipeline_replacements']
            if len(replacements)!=2*lanes or any(x['module']!='MultiplyAddReplacement' for x in replacements):
                raise AssertionError('ordinary C++ function incorrectly replaced, or MAC lanes lost')
            calls=[r for r in timing['streaming_regions'] if re.fullmatch(r'worker\.hls_call_[^.]+',r['scope'])]
            if len(calls)!=2 or any(r['initiation_interval']!=1 for r in calls):
                raise AssertionError(f'expected two separate generated source pipelines: {calls}')
            if not all(r['target_met'] for r in timing['rules']):raise AssertionError('retiming target unmet')
            results={}
            for kind,rtl,top in [('raw',sv[0],sv[0].stem),('mapped',work/'mapped/gates.v','SourceCallTop')]:
                obj=work/(kind+'-obj')
                flags=f'-std=c++17 -O1 -DSOURCE_CALL_LANES={lanes}'
                if kind=='mapped':flags+=' -DSOURCE_CALL_RETIMED'
                text=run([a.verilator,'--cc','--exe','--build','-j','2','-Wno-fatal',
                    '--top-module',top,'--prefix','VSourceCallTop','--Mdir',obj,'-CFLAGS',flags,
                    '-MAKEFLAGS',f'CXX={a.cxx} LINK={a.cxx}',rtl,tests/'SourceCallRun.cpp'],work/(kind+'-build.log'))
                if '%Warning-LATCH' in text:raise AssertionError('latch in source pipeline')
                results[kind]=run([obj/'VSourceCallTop'],work/(kind+'-run.log')).strip()
                if 'PASS ordinary calls' not in results[kind]:raise AssertionError('simulation pass marker missing')
            report['cases'].append({'lanes':lanes,'calls':calls,'replacements':replacements,'results':results});save()
            # Check throughput by executing consecutive jobs through the same
            # C++ call chain. Wider graphs may legitimately need more stages;
            # their stage count is not an initiation-interval measurement.
            stream=work/'stream';stream.mkdir()
            stream_defs=[*defs,'-DSOURCE_CALL_STREAMING']
            run([a.cxx,*stream_defs,tests/'SourceCall.cpp','-o',stream/'native'],stream/'native-build.log')
            run([stream/'native'],stream/'native-run.log')
            run([a.cpphdl,'--generated-dir',stream/'raw',tests/'SourceCall.cpp','--',
                 *stream_defs,'-DSYNTHESIS'],stream/'raw.log')
            stream_sv=list((stream/'raw').glob('cpphdl_hls_ClockedPipeline*.sv'))
            if len(stream_sv)!=1:raise AssertionError('missing generated source stream')
            run([a.cpphdl,'--synth','--top','worker','--module','SourceCallTop','--cxx',a.cxx,
                 '--output',stream/'mapped','--retiming','fit_pipeline_retiming','--clock-period-ns','4',
                 tests/'SourceCall.cpp','--',*stream_defs,'-DSYNTHESIS'],stream/'synth.log')
            stream_timing=json.loads((stream/'mapped/timing.json').read_text())
            if len(stream_timing['pipeline_replacements'])!=2*lanes:
                raise AssertionError('source stream lost a call site or parallel MAC lane')
            if not all(r['target_met'] for r in stream_timing['rules']):
                raise AssertionError('source stream retiming target unmet')
            stream_results={}
            for kind,rtl,top in [('raw',stream_sv[0],stream_sv[0].stem),
                                 ('mapped',stream/'mapped/gates.v','SourceCallTop')]:
                obj=stream/(kind+'-obj')
                text=run([a.verilator,'--cc','--exe','--build','-j','2','-Wno-fatal',
                    '--top-module',top,'--prefix','VSourceCallTop','--Mdir',obj,
                    '-CFLAGS',f'-std=c++17 -O1 -DSOURCE_CALL_LANES={lanes}',
                    '-MAKEFLAGS',f'CXX={a.cxx} LINK={a.cxx}',rtl,tests/'SourceCallStreamRun.cpp'],
                    stream/(kind+'-build.log'))
                if '%Warning-LATCH' in text:raise AssertionError('latch in source stream')
                stream_results[kind]=run([obj/'VSourceCallTop'],stream/(kind+'-run.log')).strip()
                if 'PASS source stream' not in stream_results[kind]:raise AssertionError('stream pass marker missing')
            report['cases'][-1]['stream']={'timing':stream_timing,'results':stream_results};save()
        report['status']='passed';save()
    except BaseException as e:
        report['status']='failed';report['error']=str(e);save();raise


if __name__=='__main__':main()
