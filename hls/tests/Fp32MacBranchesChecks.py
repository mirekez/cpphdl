#!/usr/bin/env python3
"""Check SYNTHESIS and real-arithmetic branches of the Verilog replacement."""
import argparse,json,hashlib,resource,subprocess,sys,tempfile,shutil
from pathlib import Path

def digest(p):return hashlib.sha256(Path(p).read_bytes()).hexdigest()
def main():
 p=argparse.ArgumentParser(description=__doc__)
 for k in ('hdlcpp','work'):p.add_argument('--'+k,type=Path,required=True)
 p.add_argument('--cxx',required=True);p.add_argument('--legacy',type=Path);p.add_argument('--latency',type=int,default=48)
 a=p.parse_args();root=Path(__file__).resolve().parents[2];work=a.work.resolve();work.mkdir(parents=True,exist_ok=True)
 report=dict(status='running',flow='hdlcpp-preserved-synthesis-real',cases=[],commands=[])
 def save():(work/'results.json').write_text(json.dumps(report,indent=2)+'\n')
 def limit():resource.setrlimit(resource.RLIMIT_AS,(12*1024**3,12*1024**3))
 def run(cmd,log):
  report['commands'].append(list(map(str,cmd)));save()
  with log.open('w') as f:subprocess.run(list(map(str,cmd)),stdout=f,stderr=f,check=True,preexec_fn=limit)
 sys.path.insert(0,str(root/'hls/math'));from generate_mac_replacement import array
 try:
  for rows,cols,latency in [(1,1,2),(2,3,3),(1,1,9),(1,1,10),(4,4,a.latency)]:
   case=Path(tempfile.mkdtemp(prefix=f'{rows}x{cols}-l{latency}-',dir=work));n=rows*cols;words=2*n+rows+cols+4
   (case/'input.sv').write_text(array(rows,cols)+f'''\nmodule Test(input clk,reset,command_valid_in,response_ready_in,
input [{words*32-1}:0] operation_in,index_in,value_in,output command_ready_out,response_valid_out,
output [{(n+1)*32-1}:0] result_out,output [31:0] fault_out);
Fp32MacArray_{rows}_{cols} #(.LATENCY({latency})) mac(.*);endmodule\n''')
   run([a.hdlcpp,'--native-graph','--preserve-synthesis','--top','Test','--output',case/'converted.cc',case/'input.sv'],case/'convert.log')
   code=(case/'converted.cc').read_text();hardware=(case/'converted.cc.synthesis.cc').read_text();simulation=(case/'converted.cc.simulation.cc').read_text()
   assert '#ifdef SYNTHESIS' in code and '\n#else\n' in code and '\n#endif\n' in code
   assert '"fp_mul"' not in hardware and '"fp_add"' not in hardware
   assert simulation.count('"fp_mul"')==n and simulation.count('"fp_add"')==n
   for label,flags in [('hardware',['-DSYNTHESIS']),('replacement',[])]:
    run([a.cxx,'-std=c++23','-O1','-pthread','-I'+str(root/'include'),*flags,case/'converted.cc','-o',case/(label+'-emit')],case/(label+'-build.log'))
    run([case/(label+'-emit'),case/(label+'.h')],case/(label+'-emit.log'))
   emitted=(case/'replacement.h').read_text()
   assert 'std::bit_cast<double>' in emitted and ' * std::bit_cast<double>' in emitted
   check=[a.cxx,'-std=c++23','-O1','-ffp-contract=off','-pthread','-I'+str(root/'include'),'-I'+str(case),
     f'-DMAC_ROWS={rows}',f'-DMAC_COLS={cols}',f'-DMAC_LATENCY={latency}',root/'hls/tests/Fp32MacBranchesCheck.cpp','-o',case/'check']
   if a.legacy and (rows,cols)==(4,4):shutil.copyfile(a.legacy,case/'legacy.h');check+=['-DLEGACY_REFERENCE']
   run(check,case/'check-build.log');run([case/'check'],case/'check.log')
   report['cases'].append(dict(rows=rows,cols=cols,latency=latency,work=str(case),fp_mul=n,fp_add=n,result=(case/'check.log').read_text()));save()
  report.update(status='passed',clocks=8192,model=str(case/'replacement.h'),legacy_sha256=digest(a.legacy) if a.legacy else None)
  paths=[f for f in work.rglob('*') if f.is_file() and f.name!='results.json']
  paths += [Path(__file__).resolve(),root/'hls/tests/Fp32MacBranchesCheck.cpp',root/'hls/math/generate_mac_replacement.py',root/'hls/math/rtl/Fp32MacSimulation.sv',root/'hls/math/rtl/EnabledDelay.sv']
  if a.legacy:paths.append(a.legacy.resolve())
  report['artifacts']={str(x):digest(x) for x in paths};save();print(work/'results.json',flush=True)
 except BaseException as e:report.update(status='failed',error=str(e));save();raise
if __name__=='__main__':main()
