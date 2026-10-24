"""Native and RTL regressions for affine maps and masked synchronous RAM."""
import argparse
from pathlib import Path
import subprocess
import sys
sys.path.insert(0,str(Path(__file__).resolve().parents[1]))
from TestToolchain import TestToolchain
p=argparse.ArgumentParser(description=__doc__)
for name in ('cpphdl','cxx','verilator','work'):p.add_argument('--'+name,type=Path,required=True)
a=p.parse_args();root=Path(__file__).resolve().parents[2];source=Path(__file__).resolve().parent
work=a.work.resolve();work.mkdir(parents=True,exist_ok=True)
tools=TestToolchain(a.cxx,a.verilator.resolve(),work)
def run(label,args):
    result=subprocess.run(tools.command(args),env=tools.env,capture_output=True,text=True,timeout=240)
    (work/(label+'.log')).write_text(result.stdout+result.stderr)
    if result.returncode or any(warning in result.stderr for warning in
            ('%Warning-MULTIDRIVEN', '%Warning-LATCH', '%Warning-UNOPTFLAT')):
        raise RuntimeError(label+'\n'+result.stdout+result.stderr)
    return result.stdout
for fixture in ('Api','Map','Ram'):
    run(fixture+'-compile',[a.cxx,'-std=c++17','-O1','-I'+str(root/'include'),source/(fixture+'Run.cc'),'-o',work/fixture])
    print(run(fixture+'-native',[work/fixture]),end='')
rtl=work/'rtl'
for path in rtl.glob('*.sv'):path.unlink()
run('convert',[a.cpphdl.resolve(),'--generated-dir',rtl,source/'DimensionTop.h','--','-I'+str(root/'include')])
files=sorted(rtl.glob('*.sv'),key=lambda f:(f.name!='Predef_pkg.sv',not f.name.endswith('_pkg.sv'),f.name))
run('dimension-lint',[a.verilator.resolve(),'--lint-only','-Wno-fatal','--top-module','DimensionTop',*files])
for fixture,top,prefix,parameters in [('Map','cpphdl_hls_ClockedMapMapAddMethods_cpphdl_hls_MapInput2_cpphdl_hls_MapOutput1','VMapAdd',['-GReads=2','-GWrites=1','-GSlots=4']),('Ram','cpphdl_hls_MaskedSynchronousRam','VMaskedRam',['-GWORDS=2','-GDEPTH=8'])]:
    obj=work/(fixture+'-obj')
    run(fixture+'-rtl-build',[a.verilator.resolve(),'--cc','--exe','--build','-j','1','-Wno-fatal',
        '--top-module',top,'--prefix',prefix,'--Mdir',obj,*parameters,'-CFLAGS','-DVERILATOR -std=c++17 -I'+str(root/'include'),
        *files,source/(fixture+'Run.cc')])
    print(run(fixture+'-rtl',[obj/prefix]),end='')
