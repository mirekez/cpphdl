#!/usr/bin/env python3
"""Validate direct C++RTL/native MACs; optionally compare their generated RTL.

hdlcpp is used only for this cross-language regression, never to generate the
normal simulation model. No Verilator or Quartus is used.
"""
import argparse
import hashlib
import json
from pathlib import Path
import resource
import subprocess
import sys
import tempfile


def digest(p):return hashlib.sha256(Path(p).read_bytes()).hexdigest()

def main():
    p=argparse.ArgumentParser(description=__doc__)
    for k in ('cpphdl','work'):p.add_argument('--'+k,type=Path,required=True)
    p.add_argument('--cxx',required=True);p.add_argument('--hdlcpp',type=Path)
    p.add_argument('--prior-rtl',type=Path,help='Previously checked 4x4, latency 48 model header')
    a=p.parse_args();root=Path(__file__).resolve().parents[2];work=a.work.resolve();work.mkdir(parents=True,exist_ok=True)
    report={'status':'running','flow':'direct-cpp-rtl','cases':[],'commands':[]}
    if a.prior_rtl:report.update(prior_rtl=str(a.prior_rtl.resolve()),prior_rtl_sha256=digest(a.prior_rtl))
    def save():(work/'results.json').write_text(json.dumps(report,indent=2)+'\n')
    def limit():resource.setrlimit(resource.RLIMIT_AS,(12*1024**3,12*1024**3))
    def run(cmd,log):
        report['commands'].append(list(map(str,cmd)));save()
        with Path(log).open('w') as out:subprocess.run(list(map(str,cmd)),stdout=out,stderr=out,check=True,preexec_fn=limit)
    try:
        run([a.cxx,'-std=c++23','-O1','-I'+str(root/'include'),root/'hls/tests/Fp32MacDirectAudit.cpp','-o',work/'audit'],work/'audit-build.log')
        for rows,cols,latency in [(1,1,2),(2,3,3),(4,4,48)]:
            case=Path(tempfile.mkdtemp(prefix=f'{rows}x{cols}-latency{latency}-',dir=work))
            flags=[f'-DMAC_ROWS={rows}',f'-DMAC_COLS={cols}',f'-DMAC_LATENCY={latency}']
            run([a.cpphdl,'--native-graph','--cpphdl',a.cpphdl,'--top','mac_replacement','--output',case,
                 '--cxx',a.cxx,'--chunk-size','512',root/'hls/tests/Fp32MacRtlArray.cpp',
                 *['--frontend-flag='+x for x in flags]],work/(case.name+'-generate.log'))
            run([work/'audit',case/'model.graph',str(rows*cols)],case/'audit.json')
            check=[a.cxx,'-std=c++23','-O1','-pthread','-I'+str(root/'include'),'-I'+str(case),*flags,
                   root/'hls/tests/Fp32MacDirectCheck.cpp','-o',case/'check']
            if a.prior_rtl and (rows,cols,latency)==(4,4,48):
                import shutil
                shutil.copyfile(a.prior_rtl,case/'replacement.h');check+=['-DRTL_REFERENCE']
            run(check,case/'check-build.log');run([case/'check'],case/'check.log')
            if a.hdlcpp:
                sys.path.insert(0,str(root/'hls/math'))
                from generate_mac_replacement import array
                words=2*rows*cols+rows+cols+4;outputs=rows*cols+1
                rtl=case/'reference.sv'
                rtl.write_text(array(rows,cols)+f'''\nmodule DirectMacReference(input wire clk, reset, command_valid_in, response_ready_in,
input wire [{words*32-1}:0] operation_in,index_in,value_in,output wire command_ready_out,response_valid_out,
output wire [{outputs*32-1}:0] result_out,output wire [31:0] fault_out);
Fp32MacArray_{rows}_{cols} #(.LATENCY({latency})) mac(.*);
endmodule\n''')
                run([a.hdlcpp,'--native-graph','--top','DirectMacReference','-DSYNTHESIS','--output',case/'rtl.cc',rtl],case/'rtl-lower.log')
                run([a.cxx,'-std=c++23','-O1','-pthread','-I'+str(root/'include'),case/'rtl.cc','-o',case/'rtl-emit'],case/'rtl-build.log')
                run([case/'rtl-emit',case/'replacement.h'],case/'rtl-emit.log')
                run([*check,'-DRTL_REFERENCE'],case/'rtl-check-build.log');run([case/'check'],case/'rtl-check.log')
            report['cases'].append(dict(rows=rows,cols=cols,latency=latency,result=(case/'check.log').read_text(),
                audit=json.loads((case/'audit.json').read_text()),rtl_checked=bool(a.hdlcpp)));save()
        report.update(status='passed',model=str(case/'model.h'),clocks=8192)
        paths=[p for p in work.rglob('*') if p.is_file() and p.name!='results.json']
        paths += [root/'hls/tests'/x for x in ('Fp32MacDirectChecks.py','Fp32MacDirectCheck.cpp','Fp32MacDirectAudit.cpp','Fp32MacRtlArray.cpp')]
        if a.prior_rtl:paths.append(a.prior_rtl.resolve())
        paths += [root/'hls/math'/x for x in ('Fp32MacRtlPipeline.h','Fp32Mac.h','FpAdd.h')]
        report['artifacts']={str(x):digest(x) for x in paths};save()
        print(work/'results.json',flush=True)
    except BaseException as error:report.update(status='failed',error=str(error));save();raise

if __name__=='__main__':main()
