#!/usr/bin/env python3
"""Native, raw RTL, and retimed RTL checks for 512-bit external memory."""
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
    args.cpphdl=args.cpphdl.resolve()
    args.cxx=args.cxx.resolve()
    args.verilator=args.verilator.resolve()
    tests=Path(__file__).resolve().parent
    root=tests.parents[1]
    work=args.work.resolve();work.mkdir(parents=True,exist_ok=True)
    source=tests/'WideMemory.cpp'
    fixture=tests/'WideMemoryRun.cpp'
    report={'status':'running','commands':[],'source_sha256':{
        str(path):hashlib.sha256(path.read_bytes()).hexdigest() for path in
        (source,fixture,tests/'WideMemoryReject.cpp',root/'hls/ExternalMemory.h',
         root/'hls/Clocked.h',root/'hls/AstClocked.cpp',
         root/'hls/delayed_scheduler.cpp',root/'synth/ScheduledGraph.h',
         root/'synth/ScheduledGraph.cpp')}}
    def save():
        (work/'results.json').write_text(json.dumps(report,indent=2)+'\n')
    def fresh(name):
        path=work/name
        if path.exists():
            for index in range(1,1000):
                old=work/f'{name}-previous-{index}'
                if not old.exists():path.rename(old);break
            else:raise RuntimeError('too many preserved wide attempts')
        path.mkdir()
        return path
    def run(command,log,expect_ok=True):
        command=[str(item) for item in command]
        report['commands'].append(command);save()
        env=os.environ.copy()
        env['PATH']=str(args.verilator.resolve().parent)+os.pathsep+env.get('PATH','')
        env['LD_LIBRARY_PATH']=str(root/'.conda/lib')+os.pathsep+env.get('LD_LIBRARY_PATH','')
        with log.open('w') as output:
            proc=subprocess.run(command,stdout=output,stderr=subprocess.STDOUT,
                                env=env,timeout=600)
        text=log.read_text()
        if (proc.returncode==0)!=expect_ok:
            raise RuntimeError(f'{log} exit {proc.returncode}:\n{text[-3000:]}')
        return text
    try:
        native=work/'native'
        run([args.cxx,'-std=c++17','-O1','-I'+str(root/'include'),source,
             '-o',native],work/'native-build.log')
        if 'PASS native wide' not in run([native],work/'native-run.log'):
            raise AssertionError('wide native numeric result absent')
        raw=fresh('raw')
        run([args.cpphdl,'--generated-dir',raw,source,'--','-std=c++17',
             '-DSYNTHESIS','-I'+str(root/'include')],work/'raw-generate.log')
        top='cpphdl_hls_ClockedMemoryWideMemoryMethods_A32_D512'
        rtl=raw/f'{top}.sv'
        text=rtl.read_text()
        if not re.search(r'output wire \[511:0\] memory_out__data_out',text) or not re.search(
            r'input wire \[511:0\] memory_out__data_in',text):
            raise AssertionError('raw 512-bit external payload width or module name missing')
        raw_obj=raw/'obj';raw_obj.mkdir()
        raw_build=run([args.verilator,'--cc','--exe','--build','-j','2','-Wno-fatal',
             '--top-module',top,'--prefix','VWideMemoryTop','--Mdir',raw_obj,
             '-CFLAGS','-std=c++17 -O1','-MAKEFLAGS',
             f'CXX={args.cxx} LINK={args.cxx}',rtl,fixture],work/'raw-build.log')
        if '%Warning-LATCH' in raw_build:
            raise AssertionError('wide raw Verilator inferred a latch')
        raw_result=run([raw_obj/'VWideMemoryTop'],work/'raw-run.log')
        if 'PASS wide raw' not in raw_result:raise AssertionError('raw wide result absent')
        mapped=fresh('mapped')
        run([args.cpphdl,'--synth','--top','worker','--module','WideMemoryTop',
             '--cxx',args.cxx,'--output',mapped,'--retiming','fit_pipeline_retiming',
             '--clock-period-ns','4','--tool-timeout','1200',source,'--',
             '-std=c++17','-DSYNTHESIS','-I'+str(root/'include')],work/'synth.log')
        timing=json.loads((mapped/'timing.json').read_text())
        rule=timing['rules'][0]
        if not rule['target_met'] or rule['target_ns']!=4 or rule['initiation_interval']<1:
            raise AssertionError(f'wide mapped retiming failed: {rule}')
        retimed_obj=mapped/'obj';retimed_obj.mkdir()
        retimed_build=run([args.verilator,'--cc','--exe','--build','-j','2','-Wno-fatal',
             '--top-module','WideMemoryTop','--prefix','VWideMemoryTop',
             '--Mdir',retimed_obj,'-CFLAGS','-std=c++17 -O1 -DWIDE_RETIMED',
             '-MAKEFLAGS',f'CXX={args.cxx} LINK={args.cxx}',
             mapped/'operations.v',fixture],work/'retimed-build.log')
        if '%Warning-LATCH' in retimed_build:
            raise AssertionError('wide retimed Verilator inferred a latch')
        retimed_result=run([retimed_obj/'VWideMemoryTop'],work/'retimed-run.log')
        if 'PASS wide retimed' not in retimed_result:
            raise AssertionError('retimed wide result absent')
        report['raw_result']=raw_result.strip()
        report['retimed_result']=retimed_result.strip()
        report['timing_rule']=rule
        for case in (1,2,3):
            reject=fresh(f'reject-{case}')
            output=run([args.cpphdl,'--generated-dir',reject,
                tests/'WideMemoryReject.cpp','--','-std=c++17','-DSYNTHESIS',
                f'-DWIDE_REJECT={case}','-I'+str(root/'include')],
                work/f'reject-{case}.log',False)
            if 'external memory access requires' not in output or list(reject.glob('*.sv')):
                raise AssertionError(f'wide rejection case {case} lacked expected diagnostic')
        report['status']='passed';save()
    except BaseException as error:
        report['status']='failed';report['error']=str(error);save();raise


if __name__=='__main__':main()
