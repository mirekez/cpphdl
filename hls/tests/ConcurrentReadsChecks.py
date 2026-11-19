#!/usr/bin/env python3
"""Check compiler-generated overlap of independent ordered external reads."""
import argparse
import hashlib
import json
import os
from pathlib import Path
import re
import subprocess


def main():
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--cpphdl',type=Path,required=True)
    parser.add_argument('--cxx',type=Path,required=True)
    parser.add_argument('--verilator',type=Path,required=True)
    parser.add_argument('--work',type=Path,required=True)
    args=parser.parse_args()
    args.cpphdl=args.cpphdl.resolve();args.cxx=args.cxx.resolve()
    args.verilator=args.verilator.resolve()
    tests=Path(__file__).resolve().parent
    root=tests.parents[1]
    work=args.work.resolve();work.mkdir(parents=True,exist_ok=True)
    source=tests/'ConcurrentReads.cpp'
    fixture=tests/'ConcurrentReadsRun.cpp'
    report={'status':'running','commands':[],'cases':[],
            'source_sha256':{str(path):hashlib.sha256(path.read_bytes()).hexdigest()
            for path in (source,fixture,root/'hls/Clocked.h',
                         root/'hls/ExternalMemory.h',root/'hls/delayed_scheduler.cpp',
                         root/'hls/AstClocked.cpp',root/'synth/retiming.cpp')}}
    def save():
        (work/'results.json').write_text(json.dumps(report,indent=2)+'\n')
    def fresh(name):
        path=work/name
        if path.exists():
            for index in range(1,1000):
                old=work/f'{name}-previous-{index}'
                if not old.exists():path.rename(old);break
            else:raise RuntimeError('too many preserved concurrent-read attempts')
        path.mkdir();return path
    def run(command,log):
        command=[str(part) for part in command]
        report['commands'].append(command);save()
        env=os.environ.copy()
        env['PATH']=str(args.verilator.parent)+os.pathsep+env.get('PATH','')
        env['LD_LIBRARY_PATH']=str(root/'.conda/lib')+os.pathsep+env.get('LD_LIBRARY_PATH','')
        with log.open('w') as output:
            process=subprocess.run(command,stdout=output,stderr=subprocess.STDOUT,
                                   env=env,timeout=900)
        text=log.read_text()
        if process.returncode:
            raise RuntimeError(f'{log} exit {process.returncode}:\n{text[-3500:]}')
        return text
    try:
        for name,window,dependent in (
            ('independent2',2,0),('independent1',1,0),('dependent2',2,1)):
            case=fresh(name)
            defines=[f'-DREAD_WINDOW={window}',f'-DDEPENDENT_SECOND_READ={dependent}']
            native=case/'native'
            run([args.cxx,'-std=c++17','-O1','-I'+str(root/'include'),
                 *defines,source,'-o',native],case/'native-build.log')
            if 'PASS native concurrent-read' not in run([native],case/'native-run.log'):
                raise AssertionError(f'{name}: native reference did not pass')
            raw=case/'raw';raw.mkdir()
            run([args.cpphdl,'--generated-dir',raw,source,'--',
                 '-std=c++17','-DSYNTHESIS',*defines,
                 '-I'+str(root/'include')],case/'raw-generate.log')
            rtl=list(raw.glob('cpphdl_hls_ClockedMemory*.sv'))
            if len(rtl)!=1:raise AssertionError(f'{name}: expected one raw worker RTL')
            module=rtl[0].stem
            if not re.search(r'\bmodule\s+\\?'+re.escape(module)+r'\b',rtl[0].read_text()):
                raise AssertionError(f'{name}: raw module name absent')
            raw_obj=raw/'obj';raw_obj.mkdir()
            flags=f'-std=c++17 -O1 -DREAD_WINDOW={window} -DDEPENDENT_SECOND_READ={dependent}'
            build=run([args.verilator,'--cc','--exe','--build','-j','2','-Wno-fatal',
                '--top-module',module,'--prefix','VConcurrentReadTop',
                '--Mdir',raw_obj,'-CFLAGS',flags,'-MAKEFLAGS',
                f'CXX={args.cxx} LINK={args.cxx}',rtl[0],fixture],case/'raw-build.log')
            if '%Warning-LATCH' in build:raise AssertionError(f'{name}: raw latch warning')
            raw_result=run([raw_obj/'VConcurrentReadTop'],case/'raw-run.log')
            mapped=case/'mapped';mapped.mkdir()
            run([args.cpphdl,'--synth','--top','worker','--module','ConcurrentReadTop',
                '--cxx',args.cxx,'--output',mapped,'--retiming','fit_pipeline_retiming',
                '--clock-period-ns','4','--tool-timeout','1200',source,'--',
                '-std=c++17','-DSYNTHESIS',*defines,
                '-I'+str(root/'include')],case/'synth.log')
            timing=json.loads((mapped/'timing.json').read_text())
            rule=timing['rules'][0]
            if not rule['target_met'] or rule['target_ns']!=4:
                raise AssertionError(f'{name}: retiming target missing: {rule}')
            obj=mapped/'obj';obj.mkdir()
            build=run([args.verilator,'--cc','--exe','--build','-j','2','-Wno-fatal',
                '--top-module','ConcurrentReadTop','--prefix','VConcurrentReadTop',
                '--Mdir',obj,'-CFLAGS',flags+' -DREAD_RETIMED','-MAKEFLAGS',
                f'CXX={args.cxx} LINK={args.cxx}',mapped/'operations.v',fixture],
                case/'retimed-build.log')
            if '%Warning-LATCH' in build:raise AssertionError(f'{name}: retimed latch warning')
            retimed_result=run([obj/'VConcurrentReadTop'],case/'retimed-run.log')
            expected=f'overlap={int(window==2 and not dependent)}'
            for kind,result in (('raw',raw_result),('retimed',retimed_result)):
                if f'PASS reads {"dependent" if dependent else "independent"} window={window}' not in result or expected not in result:
                    raise AssertionError(f'{name}: {kind} missing measured {expected}: {result}')
                stalls=re.search(r'\bstalls=(\d+)',result)
                if not stalls or int(stalls.group(1))<3:
                    raise AssertionError(f'{name}: {kind} request backpressure was not exercised: {result}')
            report['cases'].append({'name':name,'raw':raw_result.strip(),
                'retimed':retimed_result.strip(),'timing_rule':rule,
                'raw_module':module})
            save()
        if report['cases'][0]['raw_module']==report['cases'][1]['raw_module']:
            raise AssertionError('window2 and default1 raw module names collide')
        for kind in ('raw','retimed'):
            first=[]
            for case in report['cases'][:2]:
                found=re.search(r'first_latency_logical=(\d+) first_latency_clocks=(\d+)',
                                case[kind])
                if not found:raise AssertionError(f'{kind}: first-command latency absent')
                first.append(tuple(map(int,found.groups())))
            if not first[0][0]<first[1][0] or not first[0][1]<first[1][1]:
                raise AssertionError(f'{kind}: two-slot first command did not beat default: {first}')
            report[kind+'_first_latency']={'window2':first[0], 'default1':first[1]}
        report['status']='passed';save()
    except BaseException as error:
        report['status']='failed';report['error']=str(error);save();raise


if __name__=='__main__':main()
