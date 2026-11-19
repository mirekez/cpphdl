#!/usr/bin/env python3
"""Runtime source loops: carried values, FP32 dot product and shared call sites."""
import argparse
import hashlib
import json
import os
from pathlib import Path
import subprocess


def main():
    p=argparse.ArgumentParser(description=__doc__)
    for key in ('cpphdl','cxx','verilator','work'):p.add_argument('--'+key,type=Path,required=True)
    p.add_argument('--cases',type=int,nargs='+',choices=range(15),default=list(range(15)))
    a=p.parse_args()
    for key in ('cpphdl','cxx','verilator','work'):setattr(a,key,getattr(a,key).resolve())
    tests=Path(__file__).resolve().parent;root=tests.parents[1]
    a.work.mkdir(parents=True,exist_ok=True)
    env=os.environ.copy();env['PATH']=str(a.verilator.parent)+os.pathsep+str(a.cxx.parent)+os.pathsep+env.get('PATH','')
    env['LD_LIBRARY_PATH']=str(root/'.conda/lib')+os.pathsep+env.get('LD_LIBRARY_PATH','')
    inputs=[a.cpphdl,Path(__file__),tests/'SourceLoop.cpp',tests/'SourceLoopRun.cpp',tests/'SourceLoopReference.h',root/'synth/LoopPipeline.h',
            root/'synth/LocalMemory.h',root/'hls/Combinational.cpp',root/'synth/ReadPipeline.h',root/'synth/ReadArbiter.h',root/'synth/MemoryContinuation.h',
            root/'synth/ScheduledGraph.cpp',root/'synth/StreamPipeline.h',root/'hls/delayed_scheduler.cpp',root/'synth/Verilog.cpp',
            root/'hls/math/Float.h',root/'hls/math/Nonlinear.h']
    report={'status':'running','commands':[],'results':{},'timing':{},'inputs':{
        str(f):hashlib.sha256(f.read_bytes()).hexdigest() for f in inputs}}
    def save():(a.work/'results.json').write_text(json.dumps(report,indent=2)+'\n')
    def run(cmd,log):
        report['commands'].append(list(map(str,cmd)));save()
        with log.open('w') as output:
            result=subprocess.run(list(map(str,cmd)),stdout=output,stderr=subprocess.STDOUT,env=env,timeout=1200)
        text=log.read_text()
        if result.returncode:raise RuntimeError(f'{log}: {text[-4000:]}')
        return text
    def fresh(name):
        directory=a.work/name
        if directory.exists():
            for i in range(1000):
                old=a.work/f'{name}-previous-{i}'
                if not old.exists():directory.rename(old);break
            else:raise RuntimeError('too many preserved attempts')
        directory.mkdir();return directory
    try:
        for case in a.cases:
            defs=['-std=c++17','-fopenmp','-ffp-contract=off',f'-DLOOP_CASE={case}',
                  '-I'+str(root),'-I'+str(root/'include')]
            prefix=f'loop-{case}'
            run([a.cxx,*defs,tests/'SourceLoop.cpp','-o',a.work/(prefix+'-native')],a.work/(prefix+'-native-build.log'))
            report['results'][prefix+'-native']=run([a.work/(prefix+'-native')],a.work/(prefix+'-native-run.log')).strip()
            raw=fresh(prefix+'-raw');mapped=fresh(prefix+'-mapped')
            run([a.cpphdl,'--generated-dir',raw,tests/'SourceLoop.cpp','--',*defs,'-DSYNTHESIS'],a.work/(prefix+'-raw.log'))
            modules=list(raw.glob('cpphdl_hls_ClockedMemory*_Pipeline.sv'))
            if len(modules)!=1:raise AssertionError('source memory pipeline missing')
            packages=[]
            if case==11:
                package=raw/'FloatConstants_pkg.sv';packages.append(package)
                spelling=package.read_text().lower()
                for value in ("32'h3727c5ac", "32'hc0500000", "32'h80000000", "64'h8000000000000000"):
                    if value not in spelling:raise AssertionError('constexpr floating package lost exact bits: '+value)
            run([a.cpphdl,'--synth','--top','worker','--module','SourceLoopTop','--cxx',a.cxx,
                 '--output',mapped,'--retiming','fit_pipeline_retiming','--clock-period-ns','4',
                 tests/'SourceLoop.cpp','--',*defs,'-DSYNTHESIS'],a.work/(prefix+'-synth.log'))
            timing=json.loads((mapped/'timing.json').read_text());report['timing'][prefix]=timing
            if len(timing['pipeline_replacements'])!=({1:1,2:1,5:2,7:1,13:4}.get(case,0)):raise AssertionError('wrong scalar MAC boundary')
            if case==13:
                mac_latency=max(r['latency'] for r in timing['pipeline_replacements'])
                if max(r['latency'] for r in timing['streaming_regions'])>mac_latency+8:
                    raise AssertionError('independent OpenMP MAC lanes serialized across callee faults')
            if not all(r['target_met'] for r in timing['rules']):raise AssertionError('source memory retiming target unmet')
            for kind,rtl,top in [('raw',modules[0],modules[0].stem),('mapped',mapped/'operations.v','SourceLoopTop')]:
                if 'retiming_commit_out' in rtl.read_text():raise AssertionError('loops fell back to global transitions')
                obj=fresh(prefix+'-'+kind+'-obj')
                log=run([a.verilator,'--cc','--exe','--build','-j','2','-Wno-fatal',
                    '--top-module',top,'--prefix','VSourceLoopTop','--Mdir',obj,
                    '-CFLAGS',f'-std=c++17 -O1 -ffp-contract=off -DLOOP_CASE={case}',
                    '-MAKEFLAGS',f'CXX={a.cxx} LINK={a.cxx}',*(packages if kind=='raw' else []),rtl,tests/'SourceLoopRun.cpp'],a.work/(prefix+'-'+kind+'-build.log'))
                if '%Warning-LATCH' in log or '%Warning-SHORTREAL' in log:raise AssertionError('invalid source store RTL')
                result=run([obj/'VSourceLoopTop'],a.work/(prefix+'-'+kind+'-run.log')).strip()
                report['results'][prefix+'-'+kind]=result;save()
                if 'PASS source loops' not in result:raise AssertionError('missing source store simulation pass')
        report['status']='passed';save()
    except BaseException as e:
        report['status']='failed';report['error']=str(e);save();raise


if __name__=='__main__':main()
