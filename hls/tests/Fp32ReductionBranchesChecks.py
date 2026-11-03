#!/usr/bin/env python3
"""Preserve both reducer branches and compare them with FP32/retimed HLS."""
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
    for key in ('hdlcpp','work'):p.add_argument('--'+key,type=Path,required=True)
    p.add_argument('--cxx',required=True)
    p.add_argument('--design',type=Path,help='Check a linked HLS synthesis instead of standalone shapes')
    p.add_argument('--module');p.add_argument('--rows',type=int);p.add_argument('--cols',type=int)
    p.add_argument('--retimed-reference',action='store_true')
    a=p.parse_args();root=Path(__file__).resolve().parents[2];work=a.work.resolve();work.mkdir(parents=True,exist_ok=True)
    sys.path.insert(0,str(root/'hls/math'));from generate_reduction_replacement import reduction,minimum_latency
    report=dict(status='running',flow='hdlcpp-preserved-synthesis-real-reduction',commands=[],cases=[])
    def save():(work/'results.json').write_text(json.dumps(report,indent=2)+'\n')
    def limit():resource.setrlimit(resource.RLIMIT_AS,(12*1024**3,12*1024**3))
    def run(command,log,fail=False):
        report['commands'].append(list(map(str,command)));save()
        with log.open('w') as stream:r=subprocess.run(list(map(str,command)),stdout=stream,stderr=stream,preexec_fn=limit)
        if (r.returncode==0)==fail:raise RuntimeError(str(log)+'\n'+log.read_text()[-4000:])
    shapes=[(1,1,1),(2,3,3),(1,64,7),(8,8,88)]
    if a.design:
        if not a.module or not a.rows or not a.cols:p.error('Linked design needs module, rows and cols')
        timing=json.loads((a.design/'timing.json').read_text());latency=timing['streaming_regions'][0]['latency']
        if timing['pipeline_replacements'][0]['latency']!=latency:raise ValueError('Retimed replacement latency differs')
        shapes=[(a.rows,a.cols,latency)]
    try:
        for rows,cols,latency in shapes:
            case=a.design.resolve() if a.design else Path(tempfile.mkdtemp(prefix=f'{rows}x{cols}-l{latency}-',dir=work))
            module=a.module if a.design else 'Test';source=case/('gates.v' if a.design else 'input.sv')
            if not a.design:
                source.write_text(reduction(rows,cols)+f'''\nmodule Test(input clk,reset,command_valid_in,response_ready_in,
input [{(rows*cols+1)*32-1}:0] operation_in,index_in,value_in,output command_ready_out,response_valid_out,
output [{(rows+1)*32-1}:0] result_out,output [31:0] fault_out);
Fp32Reduction_{rows}_{cols} #(.LATENCY({latency})) reducer(.*);endmodule\n''')
            run([a.hdlcpp,'--native-graph','--preserve-synthesis','--top',module,'--output',case/'converted.cc',source],case/'convert.log')
            hw=(case/'converted.cc.synthesis.cc').read_text();sim=(case/'converted.cc.simulation.cc').read_text()
            assert '"fp_add"' not in hw and sim.count('"fp_add"')==rows*cols
            assert '#ifdef SYNTHESIS' in (case/'converted.cc').read_text()
            for label,flags in [('hardware',['-DSYNTHESIS']),('replacement',[])]:
                run([a.cxx,'-std=c++23','-O1','-pthread','-I'+str(root/'include'),*flags,case/'converted.cc','-o',case/(label+'-emit')],case/(label+'-compile.log'))
                run([case/(label+'-emit'),case/(label+'.h')],case/(label+'-emit.log'))
            assert ' + std::bit_cast<double>' in (case/'replacement.h').read_text()
            if a.retimed_reference:
                run([a.cxx,'-std=c++23','-O1','-pthread','-I'+str(root/'include'),case/'retimed_graph.cc','-o',case/'reference-emit'],case/'reference-compile.log')
                run([case/'reference-emit',case/'reference.h'],case/'reference-emit.log')
            command=[a.cxx,'-std=c++23','-O1','-ffp-contract=off','-pthread','-I'+str(root/'include'),'-I'+str(case),
                f'-DREDUCTION_ROWS={rows}',f'-DREDUCTION_COLS={cols}',f'-DREDUCTION_LATENCY={latency}',
                root/'hls/tests/Fp32ReductionBranchesCheck.cpp','-o',case/'check']
            if a.retimed_reference:command+=['-DRETIMED_REFERENCE']
            run(command,case/'check-compile.log');run([case/'check'],case/'check.log')
            invalid=case/'invalid.sv';invalid.write_text(source.read_text().replace(f'#(.LATENCY({latency}))',f'#(.LATENCY({minimum_latency(cols)-1}))'))
            run([a.hdlcpp,'--native-graph','--top',module,'--output',case/'invalid.cc',invalid],case/'invalid.log',fail=True)
            assert 'initial parameter assertion failed' in (case/'invalid.log').read_text()
            report['cases'].append(dict(rows=rows,cols=cols,latency=latency,work=str(case),simulation_fp_add=rows*cols,
                retimed_reference=a.retimed_reference,result=(case/'check.log').read_text()));save()
        paths=[root/'hls/math/generate_reduction_replacement.py',root/'hls/math/rtl/Fp32ReductionAddSimulation.sv',
               root/'hls/math/rtl/FpAdd.sv',root/'hls/tests/Fp32ReductionBranchesCheck.cpp',Path(__file__).resolve()]
        for c in report['cases']:
            case=Path(c['work']);paths.extend(case/n for n in ['converted.cc','converted.cc.synthesis.cc','converted.cc.simulation.cc','hardware.h','replacement.h','check','check.log','invalid.log'])
        if a.design:paths.extend(a.design/n for n in ['gates.v','timing.json','retimed_graph.cc'])
        if a.retimed_reference:paths.append(case/'reference.h')
        report.update(status='passed',clocks=8192,artifacts={str(x.resolve()):digest(x) for x in paths})
        if a.design:report.update(latency=latency,model_sha256=digest(case/'replacement.h'),verilog_sha256=digest(source))
        save();print(work/'results.json',flush=True)
    except BaseException as error:report.update(status='failed',error=str(error));save();raise


if __name__=='__main__':main()
