#!/usr/bin/env python3
"""Compare retimed original C++ HLS with linked replacement RTL via hdlcpp."""
import argparse
import json
import os
from pathlib import Path
import resource
import shutil
import subprocess
import sys


def main():
    p=argparse.ArgumentParser(description=__doc__)
    p.add_argument('--cpphdl',type=Path,required=True)
    p.add_argument('--hdlcpp',type=Path,required=True)
    p.add_argument('--work',type=Path,required=True)
    p.add_argument('--cxx',default='/usr/bin/g++')
    p.add_argument('--reuse',action='store_true')
    p.add_argument('--fp32',action='store_true')
    a=p.parse_args();root=Path(__file__).resolve().parents[2];source=Path(__file__).with_name('PipelineReplacement.cpp')
    top='PipelineTop';replacement_module='MultiplyAddReplacement'
    if a.fp32:
        source=source.with_name('Fp32MacPipeline.cpp');top='Fp32MacPipelineTop';replacement_module='Fp32MacPipeline'
    a.work.mkdir(parents=True,exist_ok=True);report=dict(status='running',verilator=False,runs=[])
    def save():(a.work/'results.json').write_text(json.dumps(report,indent=2)+'\n')
    def limit():resource.setrlimit(resource.RLIMIT_AS,(12*1024**3,12*1024**3))
    def run(command,log,fail=False):
        with log.open('w') as f:r=subprocess.run(list(map(str,command)),stdout=f,stderr=subprocess.STDOUT,preexec_fn=limit)
        if (r.returncode==0)==fail:raise RuntimeError(str(log)+'\n'+log.read_text()[-5000:])
        return log.read_text()
    try:
        save();latencies=[]
        for period in ([4] if a.fp32 else [6,2]):
            work=a.work/('period-'+str(period))
            if not a.reuse:
                if work.exists(): shutil.rmtree(work)
                run([sys.executable,root/'synth/synthesize.py','--cpphdl',a.cpphdl,'--top',top,
                    '--module',top,'--output',work,'--cxx',a.cxx,'--retiming','fit_pipeline_retiming',
                    '--clock-period-ns',str(period),source,'--','-I'+str(root)],a.work/f'synth-{period}.log')
            timing=json.loads((work/'timing.json').read_text())
            latency=timing['streaming_regions'][0]['latency'];latencies.append(latency)
            assert timing['pipeline_replacements'][0]['latency']==latency
            gates=(work/'gates.v').read_text()
            assert '#(.LATENCY('+str(latency)+'))' in gates
            cells=json.loads((work/'gates.json').read_text())['modules'][top]['cells']
            assert sum(c['type']==replacement_module for c in cells.values())==1
            assert not any(c['type']=='$_DFF_P_' for c in cells.values()),'Replaced implementation leaked into mapped graph'
            (work/'converted.cc').unlink(missing_ok=True)
            run([a.hdlcpp,'--native-graph','--top',top,'-DSYNTHESIS','--output',work/'converted.cc',work/'gates.v'],work/'hdlcpp.log')
            for source_name,header in [('retimed_graph','reference'),('converted','replacement')]:
                run([a.cxx,'-std=c++17','-O1','-pthread','-I'+str(root/'include'),work/(source_name+'.cc'),'-o',work/(header+'-emit')],work/(header+'-compile.log'))
                run([work/(header+'-emit'),work/(header+'.h')],work/(header+'-emit.log'))
            run([a.cxx,'-std=c++17','-O0','-pthread',*(['-DFP32_MAC_CHECK'] if a.fp32 else []),'-I'+str(root/'include'),'-I'+str(work),
                 Path(__file__).with_name('PipelineReplacementCheck.cpp'),'-o',work/'check'],work/'check-compile.log')
            output=run([work/'check'],work/'check.log')
            report['runs'].append(dict(period_ns=period,latency=latency,result=output.strip()));save()
        if not a.fp32:assert len(set(latencies))==2,'Retiming must actually change replacement latency'
        # A too-small parameter must fail conversion, not become a silent model.
        bad=a.work/'invalid-latency.sv';bad.write_text(gates.replace('#(.LATENCY('+str(latency)+'))','#(.LATENCY(1))'))
        (a.work/'invalid.cc').unlink(missing_ok=True)
        message=run([a.hdlcpp,'--native-graph','--top',top,'--output',a.work/'invalid.cc',bad],a.work/'invalid.log',fail=True)
        assert 'initial parameter assertion failed' in message
        # Accept only constant parameter guards. General initial behavior must
        # not be discarded merely because the replacement uses an assertion.
        for name,body in [('runtime','initial if(a) $fatal(1,"bad");'),
                          ('assignment','initial q=1;'),('timing','initial #5 q=1;')]:
            source=a.work/(name+'.sv');source.write_text('module Guard(input logic clk,a,output logic q); '+body+' always @(posedge clk) q<=a; endmodule\n')
            output=a.work/(name+'.cc');output.unlink(missing_ok=True)
            run([a.hdlcpp,'--native-graph','--top','Guard','--output',output,source],a.work/(name+'.log'),fail=True)
        report.update(status='passed',invalid_latency_rejected=True,nonconstant_initial_behavior_rejected=True);save()
    except BaseException as error:
        report.update(status='failed',error=str(error));save();raise


if __name__=='__main__':main()
