#!/usr/bin/env python3
"""Whole-beat source copies through the streaming scheduler and synchronous RAM."""
import argparse
import hashlib
import json
import os
from pathlib import Path
import subprocess


def main():
    parser=argparse.ArgumentParser(description=__doc__)
    for name in ('cpphdl','cxx','verilator','work'):
        parser.add_argument('--'+name,type=Path,required=True)
    parser.add_argument('--bits',type=int,nargs='+',choices=(128,256,512),default=[128,256,512])
    parser.add_argument('--groups',action='store_true',help='test joins of independent wide local loads')
    args=parser.parse_args()
    for name in ('cpphdl','cxx','verilator','work'):setattr(args,name,getattr(args,name).resolve())
    tests=Path(__file__).resolve().parent;root=tests.parents[1]
    args.work.mkdir(parents=True,exist_ok=True)
    name='WideReadGroup' if args.groups else 'WideStreamCopy'
    source=tests/(name+'.cpp');bench=tests/(name+'Run.cpp')
    paths=[args.cpphdl,source,bench,Path(__file__),root/'synth/CopyLoop.h',root/'synth/ReadPipeline.h',
           root/'synth/LocalMemory.h',root/'synth/LoopPipeline.h',root/'synth/ReadGroup.h',
           root/'synth/ScheduledGraph.cpp',root/'hls/delayed_scheduler.cpp']
    report={'status':'running','sources':{str(p):hashlib.sha256(p.read_bytes()).hexdigest() for p in paths},
            'commands':[],'results':{},'timing':{}}
    env=os.environ.copy();env['PATH']=str(args.cxx.parent)+os.pathsep+str(args.verilator.parent)+os.pathsep+env.get('PATH','')
    env['LD_LIBRARY_PATH']=str(root/'.conda/lib')+os.pathsep+env.get('LD_LIBRARY_PATH','')
    def save():(args.work/'results.json').write_text(json.dumps(report,indent=2)+'\n')
    def run(command,log):
        command=list(map(str,command));report['commands'].append(command);save()
        with log.open('w') as output:proc=subprocess.run(command,stdout=output,stderr=subprocess.STDOUT,env=env,timeout=1200)
        text=log.read_text()
        if proc.returncode:raise RuntimeError(f'{log}: {text[-3000:]}')
        return text.strip()
    try:
        for bits in args.bits:
            work=args.work/str(bits);work.mkdir(exist_ok=False)
            flags=['-std=c++17','-fopenmp',f'-DSTREAM_BITS={bits}','-I'+str(root/'include')]
            run([args.cxx,*flags,source,'-o',work/'native'],work/'native-build.log')
            report['results'][f'{bits}-native']=run([work/'native'],work/'native-run.log')
            raw=work/'raw';mapped=work/'mapped'
            run([args.cpphdl,'--generated-dir',raw,source,'--',*flags,'-DSYNTHESIS'],work/'generate.log')
            modules=list(raw.glob('*_Pipeline.sv'))
            if len(modules)!=1:raise AssertionError('missing raw streaming module')
            run([args.cpphdl,'--synth','--top','worker','--module','WideStreamCopyTop','--cxx',args.cxx,
                 '--output',mapped,'--operations-only','--retiming','fit_pipeline_retiming','--clock-period-ns','4',
                 source,'--',*flags,'-DSYNTHESIS'],work/'synth.log')
            graph=(mapped/'retimed_graph.cc').read_text()
            expected='"hls_independent_local_loads"' if args.groups else '"hls_copy_loop_pipeline"'
            if expected not in graph:raise AssertionError('missing wide copy/group pipeline')
            if f'"hls_local_memory_port_bits" "{bits}"' not in graph:raise AssertionError('wrong local RAM port width')
            timing=json.loads((mapped/'timing.json').read_text());report['timing'][str(bits)]=timing
            if not all(r['target_met'] for r in timing['rules']):raise AssertionError('wide pipeline missed retiming target')
            for kind,rtl,top in [('raw',modules[0],modules[0].stem),('mapped',mapped/'operations.v','WideStreamCopyTop')]:
                obj=work/(kind+'-obj')
                build=run([args.verilator,'--cc','--exe','--build','-j','2','-Wno-fatal','--top-module',top,
                    '--prefix','VWideStreamCopyTop','--Mdir',obj,'-CFLAGS',f'-std=c++17 -O1 -DSTREAM_BITS={bits}',
                    '-MAKEFLAGS',f'CXX={args.cxx} LINK={args.cxx}',rtl,bench],work/(kind+'-build.log'))
                if '%Warning-LATCH' in build:raise AssertionError('latch in wide streaming pipeline')
                result=run([obj/'VWideStreamCopyTop'],work/(kind+'-run.log'))
                if 'PASS wide streaming' not in result:raise AssertionError('missing wide simulation pass')
                report['results'][f'{bits}-{kind}']=result;save()
        report['status']='passed'
    except BaseException as error:
        report.update(status='failed',error=str(error));raise
    finally:save()


if __name__=='__main__':main()
