#!/usr/bin/env python3
"""Rebuild and qualify one physical IHP SAR slice; never commit or publish files."""
import argparse,json,os,shlex,shutil,subprocess,sys
from pathlib import Path
from provenance import ROOT,digest,snapshot,verify,receipt
from layout import build
from physical import run as physical
from backannotate import bind
from process import lower
from case import run as run_case
HERE=Path(__file__).resolve().parent

def run(a):
    work=a.work.resolve()
    if work.exists() and any(work.iterdir()):raise ValueError('use an empty work directory to retain previous evidence')
    work.mkdir(parents=True,exist_ok=True);env=dict(os.environ,OMP_NUM_THREADS='1',PDK_ROOT=str(a.pdk),PDK='ihp-sg13g2')
    def command(name,args,timeout=7200):
        with (work/(name+'.log')).open('w') as log:r=subprocess.run(list(map(str,args)),cwd=ROOT,env=env,stdout=log,stderr=-2,timeout=timeout)
        if r.returncode:raise RuntimeError(name+' failed; see '+str(work/(name+'.log')))
    flags=['-O3','-DNDEBUG','-std=c++20','-I'+str(ROOT/'analog'),'-I'+str(ROOT/'analog/include'),'-I'+str(ROOT/'include'),'-I'+str(a.eigen),'-I'+str(a.osdi_include)]
    inputs=snapshot(a.pdk,[a.osdi_include/'osdi.h',a.magic])
    if a.order:inputs[str(a.order.resolve())]=digest(a.order)
    for source in ('native.cpp','export.cpp'):
        deps=subprocess.check_output([a.cxx,*flags,'-MM',str(HERE/source)],text=True,cwd=ROOT).replace('\\\n',' ')
        for name in shlex.split(deps.split(':',1)[1]):
            p=(ROOT/name).resolve();inputs[str(p)]=digest(p)
    (work/'inputs.json').write_text(json.dumps(inputs,indent=2)+'\n')
    result=dict(status='RUNNING',qualified=False,stages={},work=str(work),scope='one 8-bit analog slice; external SAR sequencer; deterministic MOS corners, nominal resistor/capacitor process')
    def save():
        (work/'checks.json').write_text(json.dumps(result,indent=2)+'\n')
    save()
    try:
        command('compile-export',[a.cxx,*flags,HERE/'export.cpp','-o',work/'export'])
        command('export',[work/'export',work/'exported'])
        command('parallel-mim-tests',[sys.executable,HERE/'test_parallel_capacitor.py','--pdk',a.pdk])
        geometry=build(json.loads((work/'exported/circuit.json').read_text()),a.pdk,work/'placement',json.loads(a.order.read_text()) if a.order else None,a.strengthen_track)
        placement=work/'placement';shutil.copy2(placement/'adc_sar_slice.gds',work/'unfilled.gds')
        command('fill',[a.tools/'bin/klayout','-zz','-r',HERE/'fill.py','-rd','output_file='+str(placement/'adc_sar_slice.gds'),work/'unfilled.gds'])
        result['stages']['physical']=physical(placement,work/'physical',a.pdk,a.tools,a.magic);save()
        if result['stages']['physical']['status']!='PASS':raise ValueError('physical checks failed')
        annotation=bind(placement,work/'physical',work/'backannotation',a.pdk)
        result['stages']['backannotation']=dict(status='PASS',junctions=annotation['junction']['devices'],reduction=annotation['reduction']);save()
        command('backannotation-tests',[sys.executable,HERE/'test_backannotation.py','--placement',placement,'--physical',work/'physical','--pdk',a.pdk])
        command('waveform-metric-tests',[sys.executable,HERE/'test_compare.py'])
        command('report-evidence-tests',[sys.executable,HERE/'test_report.py'])
        command('matched-placement-tests',[sys.executable,HERE/'test_matched_order.py'])
        command('source-access-tests',[sys.executable,HERE/'test_source_access.py','--placement',placement])
        command('loading-tests',[sys.executable,HERE/'test_loading.py'])
        command('loading',[sys.executable,HERE/'loading.py',
                '--circuit',work/'backannotation/native/circuit.txt',
                '--ext',work/'physical/extraction/named/ExtractedAnalog.ext',
                '--placement',placement/'placement.json',
                '--output',work/'comparator-loading.json'])
        command('wire-metric-tests',[sys.executable,HERE/'test_rc_metrics.py'])
        command('wire-metrics',[sys.executable,HERE/'rc_metrics.py',
                '--circuit',work/'backannotation/native/circuit.txt',
                '--ext',work/'physical/extraction/named/ExtractedAnalog.res.ext',
                '--placement',placement/'placement.json',
                '--output',work/'wire-resistance.json'])
        if not a.physical_only:
            command('compile-native',[a.cxx,*flags,HERE/'native.cpp','-ldl','-o',work/'native'])
            command('compile-solver-test',[a.cxx,*flags,ROOT/'analog/tests/singleton_linear.cpp','-o',work/'solver-test'])
            command('solver-test',[work/'solver-test'])
            compiled=dict(inputs);compiled[str(work/'native')]=digest(work/'native')
            build_receipt=work/'native-build.json'
            build_receipt.write_text(json.dumps(compiled,indent=2)+'\n')
            for flow in ('schematic','extracted'):
                netlist=placement/'adc_sar_slice.schematic.spice' if flow=='schematic' else work/'backannotation/adc_sar_slice.extracted.spice'
                observations=geometry['observations'] if flow=='schematic' else {k:v['extracted_node'] for k,v in annotation['observations'].items()}
                for label,corner,temp in [('tt','mos_tt',27),('ss','mos_ss',125),('ff','mos_ff',-40)]:
                    case=work/(flow+'-'+label);binding=work/'bindings'/(flow+'-'+label)
                    lower(netlist.read_text(),a.pdk,binding,temp,corner,observations)
                    result['stages'][flow+'-'+label]=run_case(binding,work/'native',case,a.step,a.degree,a.levels,build_receipt,a.timeout)
                    save()
            # Refinement and independent PSP replay are separate required rows
            # in pnr.md; this runner does not silently claim they were performed.
        result['status']='PASS' if all(r['status']=='PASS' for r in result['stages'].values()) else 'FAIL'
        result['qualified']=False # DNL/INL, mismatch/noise, reliability and independent parity need their own evidence.
        result['scope']+='; PASS here covers executed checks only, not tapeout qualification'
        save();verify(inputs)
        receipt(work/'manifest.json',inputs,[p for p in work.rglob('*') if p.is_file() and p.name not in ('manifest.json','inputs.json')],status=result['status'],qualified=False)
    except Exception as e:
        result.update(status='FAIL',error=str(e));save();raise
    return result

if __name__=='__main__':
    p=argparse.ArgumentParser(description=__doc__)
    p.add_argument('--work',type=Path,required=True)
    p.add_argument('--pdk',type=Path,default=ROOT/'analog/pdk/IHPSG13G2')
    p.add_argument('--tools',type=Path,default=ROOT/'analog/out/repeater-toolchain')
    p.add_argument('--magic',type=Path,default=ROOT/'analog/out/magic-double/install/bin/magic')
    p.add_argument('--eigen',type=Path,default=ROOT/'build/rx-tooldeps/usr/include/eigen3')
    p.add_argument('--osdi-include',type=Path,default=ROOT/'build/adc-model-closure/osdi-sdk')
    p.add_argument('--cxx',default='c++');p.add_argument('--step',type=float,default=20e-12);p.add_argument('--degree',type=int,default=32);p.add_argument('--timeout',type=int,default=7200)
    default=HERE/'placement_order.json'
    p.add_argument('--order',type=Path,default=default if default.exists() else None,help='fixed paired order for the qualified sizing candidate')
    p.add_argument('--strengthen-track',action='store_true',help='reinforce the physical TRACK bus; requires fresh physical/electrical checks')
    p.add_argument('--levels',nargs='+',type=float,default=[.85,1.15,1.45,1.75,2.05]);p.add_argument('--physical-only',action='store_true')
    a=p.parse_args()
    for name in ('pdk','tools','magic','eigen','osdi_include'):setattr(a,name,getattr(a,name).resolve())
    r=run(a);print(r['status']);sys.exit(r['status']!='PASS')
