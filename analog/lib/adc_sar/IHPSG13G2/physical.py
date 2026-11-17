#!/usr/bin/env python3
"""Run actual IHP DRC/LVS, OpenROAD database import and Magic RC extraction."""
import argparse,concurrent.futures,json,os,subprocess,sys,traceback
from collections import Counter
from pathlib import Path
import xml.etree.ElementTree as ET
ROOT=Path(__file__).resolve().parents[4]
sys.path[:0]=[str(ROOT/'analog/1G/IHPSG13G2'),str(ROOT/'analog/pdk/1G_repeater_IHPSG13G2')]
from checks import check_lvs,check_drc,check_drc_completion
from named_node_extraction import extract_named
from extract_analog import digest


def run(placement,work,pdk,tools,magic):
    if work.exists() and any(work.iterdir()):raise ValueError('physical evidence directory must be empty; stale reports cannot qualify a new run')
    work.mkdir(parents=True,exist_ok=True);g=json.loads((placement/'placement.json').read_text())
    tech=pdk/'ihp-sg13g2/libs.tech/klayout/tech';gds=placement/'adc_sar_slice.gds'
    files=[Path(__file__).resolve(),Path(sys.modules['checks'].__file__).resolve(),
        Path(extract_named.__code__.co_filename).resolve(),magic,tools/'bin/klayout',tools/'bin/openroad']
    # The installed bin entries are environment wrappers. Record the actual
    # executables and KLayout engine libraries as well as those wrappers.
    for name in ('klayout','openroad'):
        binary=tools/'root/usr/bin'/name
        if binary.is_file():files.append(binary)
    engine=tools/'root/usr/lib/klayout'
    if engine.exists():files.extend(p for p in engine.glob('*.so*') if p.is_file())
    files += [placement/name for name in ('adc_sar_slice.gds','adc_sar_slice.lvs.cir','adc_sar_slice.cells.lef','adc_sar_slice.def','placement.json')]
    files += [p for folder in (tech/'drc',tech/'lvs') for p in folder.rglob('*') if p.is_file() and '__pycache__' not in p.parts and p.suffix in ('.py','.json','.drc','.lvs','.rb','.lym')]
    inputs={str(p.resolve()):digest(p) for p in files}
    env=dict(os.environ,PATH=str(tools/'bin')+os.pathsep+os.environ['PATH'],PDK_ROOT=str(pdk),PDK='ihp-sg13g2',OMP_NUM_THREADS='1')
    def command(name,args):
        with (work/(name+'.log')).open('w') as log:
            r=subprocess.run(list(map(str,args)),cwd=work,env=env,stdout=log,stderr=-2,timeout=1800)
        return r.returncode
    def lvs():
        rc=command('lvs',[sys.executable,tech/'lvs/run_lvs.py','--layout',gds,'--netlist',placement/'adc_sar_slice.lvs.cir','--topcell','adc_sar_slice','--run_mode','deep','--run_dir',work/'lvs'])
        if rc:raise RuntimeError('LVS tool returned failure')
        report=work/'lvs/adc_sar_slice.lvsdb'
        return dict(status='PASS',returncode=rc,**check_lvs(report,'adc_sar_slice',set(g['ports']),59))
    def drc():
        rc=command('drc',[sys.executable,tech/'drc/run_drc.py','--path',gds,'--topcell','adc_sar_slice','--run_dir',work/'drc','--mp','2','--density_thr','1','--antenna'])
        if rc:raise RuntimeError('DRC tool returned failure')
        path=work/'drc/adc_sar_slice_adc_sar_slice_full.lyrdb';completion=check_drc_completion(path,tech/'drc')
        violations=Counter(x.findtext('category').strip("'") for x in ET.parse(path).getroot().find('items'))
        result=dict(status='FAIL' if violations else 'PASS',violations=dict(violations),count=sum(violations.values()),completion=completion,returncode=rc)
        if not violations:result['qualification']=check_drc(path,tech/'drc')
        return result
    def openroad():
        tcl=work/'placement.tcl';technology=pdk/'ihp-sg13g2/libs.ref/sg13g2_stdcell/lef/sg13g2_tech.lef'
        tcl.write_text(f'read_lef {{{technology}}}\nread_lef {{{placement/"adc_sar_slice.cells.lef"}}}\nread_def {{{placement/"adc_sar_slice.def"}}}\n'+
            'set block [ord::get_db_block]\n'+
            'set f [open placement_counts.txt w]\nputs $f "[llength [$block getInsts]] [llength [$block getNets]] [llength [$block getBTerms]]"\nclose $f\n'+
            f'write_db {{{work/"adc_sar_slice.odb"}}}\nwrite_def {{{work/"adc_sar_slice.def"}}}\n')
        rc=command('openroad',[tools/'bin/openroad','-exit',tcl])
        if rc:raise RuntimeError('OpenROAD import failed')
        counts=list(map(int,(work/'placement_counts.txt').read_text().split()));want=g['placement_view']
        if counts!=[want[k] for k in ('components','nets','pins')]:raise ValueError('OpenROAD changed placement connectivity')
        return dict(status='PASS',components=counts[0],nets=counts[1],pins=counts[2],scope='fixed placement/connectivity imported; analog timing is verified electrically, not with digital STA')
    def extraction():
        r=extract_named(gds,'adc_sar_slice',g['ports'],pdk,magic,work/'extraction','VSS')
        return dict(status='PASS',inventory=r['inventory'],report=str(work/'extraction/checks.json'))
    result=dict(status='RUNNING',placement=str(placement),source_sha256=inputs,scope='actual one-slice GDS qualification; no waived DRC categories',stages={})
    with concurrent.futures.ThreadPoolExecutor(max_workers=3) as pool:
        futures={pool.submit(fn):name for name,fn in [('lvs',lvs),('drc',drc),('openroad',openroad),('extraction',extraction)]}
        for future in concurrent.futures.as_completed(futures):
            name=futures[future]
            try:result['stages'][name]=future.result()
            except Exception as e:
                result['stages'][name]=dict(status='FAIL',error=str(e));(work/(name+'_error.txt')).write_text(traceback.format_exc())
            (work/'checks.json').write_text(json.dumps(result,indent=2)+'\n');print(name,result['stages'][name]['status'],flush=True)
    result['status']='PASS' if all(r['status']=='PASS' for r in result['stages'].values()) else 'FAIL'
    changed=[p for p,h in inputs.items() if not Path(p).is_file() or digest(p)!=h]
    if changed:result.update(status='FAIL',changed_inputs=changed)
    result['artifact_sha256']={str(p.relative_to(work)):digest(p) for p in work.rglob('*') if p.is_file() and p.name!='checks.json'}
    (work/'checks.json').write_text(json.dumps(result,indent=2)+'\n')
    return result

if __name__=='__main__':
    p=argparse.ArgumentParser(description=__doc__)
    for name in ('placement','work','pdk','tools','magic'):p.add_argument('--'+name,type=Path,required=True)
    a=p.parse_args();r=run(*(getattr(a,name).resolve() for name in ('placement','work','pdk','tools','magic')));print('Physical status:',r['status']);sys.exit(r['status']!='PASS')
