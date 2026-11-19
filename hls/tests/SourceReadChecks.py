#!/usr/bin/env python3
"""Source read -> ordered contexts -> ordinary pipelined function calls."""
import argparse
import hashlib
import json
import os
from pathlib import Path
import re
import subprocess


def main():
    p=argparse.ArgumentParser(description=__doc__)
    for key in ('cpphdl','cxx','verilator','work'):p.add_argument('--'+key,type=Path,required=True)
    p.add_argument('--cases',type=int,nargs='+',choices=range(7),default=list(range(7)))
    a=p.parse_args()
    for key in ('cpphdl','cxx','verilator','work'):setattr(a,key,getattr(a,key).resolve())
    tests=Path(__file__).resolve().parent;root=tests.parents[1]
    a.work.mkdir(parents=True,exist_ok=True)
    env=os.environ.copy();env['PATH']=str(a.verilator.parent)+os.pathsep+str(a.cxx.parent)+os.pathsep+env.get('PATH','')
    env['LD_LIBRARY_PATH']=str(root/'.conda/lib')+os.pathsep+env.get('LD_LIBRARY_PATH','')
    report={'status':'running','commands':[],'results':{},'inputs':{
        str(f):hashlib.sha256(f.read_bytes()).hexdigest() for f in
        (a.cpphdl,tests/'SourceCall.cpp',tests/'SourceReadRun.cpp',tests/'SourceReadReject.cpp',
         tests/'SourceMultiRead.cpp',tests/'SourceMultiReadRun.cpp',root/'synth/ReadArbiter.h',
         tests/'Fp32MacSimulationCheck.cpp',root/'hls/math/rtl/Fp32MacSimulation.sv',
         root/'synth/ReadPipeline.h',root/'synth/MemoryContinuation.h',root/'hls/delayed_scheduler.cpp')}}
    def save():(a.work/'results.json').write_text(json.dumps(report,indent=2)+'\n')
    def run(cmd,log,ok=True):
        report['commands'].append(list(map(str,cmd)));save()
        with log.open('w') as output:
            result=subprocess.run(list(map(str,cmd)),stdout=output,stderr=subprocess.STDOUT,env=env,timeout=1200)
        text=log.read_text()
        if (result.returncode==0)!=ok:raise RuntimeError(f'{log}: {text[-4000:]}')
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
        defs=['-std=c++17','-fopenmp','-ffp-contract=off','-DSOURCE_CALL_LANES=2',
              '-DSOURCE_CALL_MEMORY_STREAMING','-I'+str(root),'-I'+str(root/'include')]
        run([a.cxx,*defs,tests/'SourceCall.cpp','-o',a.work/'native'],a.work/'native-build.log')
        run([a.work/'native'],a.work/'native-run.log')
        raw=fresh('raw');mapped=fresh('mapped')
        run([a.cpphdl,'--generated-dir',raw,tests/'SourceCall.cpp','--',*defs,'-DSYNTHESIS'],a.work/'raw.log')
        modules=list(raw.glob('cpphdl_hls_ClockedMemory*_Pipeline.sv'))
        if len(modules)!=1:raise AssertionError('streaming memory source module missing')
        run([a.cpphdl,'--synth','--top','worker','--module','SourceReadTop','--cxx',a.cxx,
            '--output',mapped,'--retiming','fit_pipeline_retiming','--clock-period-ns','4',
            tests/'SourceCall.cpp','--',*defs,'-DSYNTHESIS'],a.work/'synth.log')
        timing=json.loads((mapped/'timing.json').read_text());report['timing']=timing
        regions={r['scope'] for r in timing['streaming_regions']}
        if regions!={'worker.request','worker.continuation'}:
            raise AssertionError(f'expected request and continuation pipelines, got {regions}')
        if len(timing['pipeline_replacements'])!=4:
            raise AssertionError('source call sites or OpenMP MAC lanes lost')
        if not all(r['target_met'] for r in timing['rules']):raise AssertionError('retiming target unmet')
        for kind,rtl,top in [('raw',modules[0],modules[0].stem),('mapped',mapped/'operations.v','SourceReadTop')]:
            text=rtl.read_text()
            if 'retiming_commit_out' in text:raise AssertionError('memory pipeline fell back to global transitions')
            if not re.search(r'reg\s+\[63:0\]\s+__cpphdl_memory_\d+\s*\[0:7\]',text):
                raise AssertionError('context queue is missing its RAM array')
            obj=fresh(kind+'-obj')
            log=run([a.verilator,'--cc','--exe','--build','-j','2','-Wno-fatal',
                '--top-module',top,'--prefix','VSourceReadTop','--Mdir',obj,'-CFLAGS','-std=c++17 -O1 -DSOURCE_CALL_LANES=2',
                '-MAKEFLAGS',f'CXX={a.cxx} LINK={a.cxx}',rtl,tests/'SourceReadRun.cpp'],a.work/(kind+'-build.log'))
            if '%Warning-LATCH' in log:raise AssertionError('latch in memory continuation')
            report['results'][kind]=run([obj/'VSourceReadTop'],a.work/(kind+'-run.log')).strip();save()
            if 'PASS source read pipeline' not in report['results'][kind]:raise AssertionError('missing simulation pass')
        for case in a.cases:
            defs=['-std=c++17','-fopenmp','-ffp-contract=off',f'-DMULTI_READ_CASE={case}',
                  '-I'+str(root),'-I'+str(root/'include')]
            prefix=f'multi-{case}'
            run([a.cxx,*defs,tests/'SourceMultiRead.cpp','-o',a.work/(prefix+'-native')],a.work/(prefix+'-native-build.log'))
            run([a.work/(prefix+'-native')],a.work/(prefix+'-native-run.log'))
            raw=fresh(prefix+'-raw');mapped=fresh(prefix+'-mapped')
            run([a.cpphdl,'--generated-dir',raw,tests/'SourceMultiRead.cpp','--',*defs,'-DSYNTHESIS'],a.work/(prefix+'-raw.log'))
            modules=list(raw.glob('cpphdl_hls_ClockedMemory*_Pipeline.sv'))
            if len(modules)!=1:raise AssertionError('multiple-read source module missing')
            run([a.cpphdl,'--synth','--top','worker','--module','SourceMultiReadTop','--cxx',a.cxx,
                 '--output',mapped,'--retiming','fit_pipeline_retiming','--clock-period-ns','4',
                 tests/'SourceMultiRead.cpp','--',*defs,'-DSYNTHESIS'],a.work/(prefix+'-synth.log'))
            timing=json.loads((mapped/'timing.json').read_text());report[prefix+'-timing']=timing
            if len(timing['pipeline_replacements'])!=({2:2,6:1}.get(case,0)):
                raise AssertionError('source helper was duplicated across a memory cut or lost its OpenMP lanes')
            if not all(r['target_met'] for r in timing['rules']):raise AssertionError('multiple-read retiming target unmet')
            for kind,rtl,top in [('raw',modules[0],modules[0].stem),('mapped',mapped/'operations.v','SourceMultiReadTop')]:
                if 'retiming_commit_out' in rtl.read_text():raise AssertionError('multiple reads fell back to global transitions')
                obj=fresh(prefix+'-'+kind+'-obj')
                log=run([a.verilator,'--cc','--exe','--build','-j','2','-Wno-fatal',
                    '--top-module',top,'--prefix','VSourceMultiReadTop','--Mdir',obj,
                    '-CFLAGS',f'-std=c++17 -O1 -ffp-contract=off -DMULTI_READ_CASE={case}',
                    '-MAKEFLAGS',f'CXX={a.cxx} LINK={a.cxx}',rtl,tests/'SourceMultiReadRun.cpp'],a.work/(prefix+'-'+kind+'-build.log'))
                if '%Warning-LATCH' in log:raise AssertionError('latch in multiple-read pipeline')
                if '%Warning-SHORTREAL' in log:raise AssertionError('unsupported FP32 simulation conversion')
                result=run([obj/'VSourceMultiReadTop'],a.work/(prefix+'-'+kind+'-run.log')).strip()
                report['results'][prefix+'-'+kind]=result;save()
                if 'PASS multiple source reads' not in result:raise AssertionError('missing multiple-read simulation pass')
        if 6 in a.cases:
            obj=fresh('fp32-primitive-obj')
            log=run([a.verilator,'--cc','--exe','--build','-j','2','-Wno-fatal',
                '--top-module','Fp32Mac','--prefix','VFp32MacSimulation','--Mdir',obj,
                '-CFLAGS','-std=c++17 -O1 -ffp-contract=off',
                '-MAKEFLAGS',f'CXX={a.cxx} LINK={a.cxx}',root/'hls/math/rtl/Fp32MacSimulation.sv',
                tests/'Fp32MacSimulationCheck.cpp'],a.work/'fp32-primitive-build.log')
            if '%Warning-SHORTREAL' in log or '%Warning-LATCH' in log:raise AssertionError('invalid fast FP32 primitive')
            report['results']['fp32-primitive']=run([obj/'VFp32MacSimulation'],a.work/'fp32-primitive-run.log').strip();save()
        for case,message in ((5,'volatile/atomic access is not supported'),
                             (4,'same unsigned integer width')):
            log=run([a.cpphdl,'--generated-dir',fresh(f'reject-{case}'),tests/'SourceReadReject.cpp','--',
                '-std=c++17','-DSYNTHESIS',f'-DREAD_REJECT_CASE={case}','-I'+str(root/'include')],
                a.work/f'reject-{case}.log',False)
            if message not in log:raise AssertionError(f'unsupported source did not explain its limitation: {log}')
        report['status']='passed';save()
    except BaseException as e:
        report['status']='failed';report['error']=str(e);save();raise


if __name__=='__main__':main()
