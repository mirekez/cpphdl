#!/usr/bin/env python3
"""Bind conserved extracted RC and GDS-proved junctions to native Analog++."""
import argparse,json,re,sys
from pathlib import Path
ROOT=Path(__file__).resolve().parents[4]
sys.path.insert(0,str(ROOT/'analog/1G/IHPSG13G2'))
from mos_junction_extraction import restore
from resistor_body_extraction import adapt
from reduce_extracted_wires import reduce
from extract_analog import digest
from process import lower


def bind(placement,physical,work,pdk,corner='mos_tt',temp=27):
    work.mkdir(parents=True,exist_ok=True)
    checks=json.loads((physical/'checks.json').read_text())
    if checks['status']!='PASS':raise ValueError('physical qualification must pass before back annotation')
    ext=physical/'extraction';g=json.loads((placement/'placement.json').read_text())
    receipt=json.loads((ext/'checks.json').read_text())
    if receipt['source_sha256'].get(str((placement/'adc_sar_slice.gds').resolve()))!=digest(placement/'adc_sar_slice.gds'):
        raise ValueError('GDS differs from extraction receipt')
    text=(ext/'conserved.spice').read_text()
    text,junction=restore(placement/'adc_sar_slice.gds',g,ext/'named/ExtractedAnalog.res.ext',text,include_lv=True)
    text,body=adapt((ext/'named/capacitance.spice').read_text(),text,(ext/'named/ExtractedAnalog.ext').read_text(),g['ports'])
    # Resolve PCell labels through Magic's explicit equipotential aliases. The
    # selected canonical node is retained by exact wire reduction, not shorted
    # to all other points on that distributed conductor.
    parent={}
    def root(n):
        parent.setdefault(n,n)
        if parent[n]!=n:parent[n]=root(parent[n])
        return parent[n]
    raw=(ext/'named/ExtractedAnalog.ext').read_text()
    for a,b in re.findall(r'^equiv "([^"]+)" "([^"]+)"',raw,re.M):parent[root(b)]=root(a)
    available=set()
    for line in text.splitlines():
        f=line.split()
        if f and f[0][0] in 'RC':available.update(f[1:3])
    observations={};mapping={}
    for label,logical in g['observations'].items():
        candidates=[]
        for part in g['components']:
            for pin,net in part['connections'].items():
                if net==logical:
                    alias=part['name']+'_0.'+pin
                    if root(alias) in available:candidates.append((alias,root(alias)))
        if not candidates or len({n for _,n in candidates})!=1:raise ValueError('ambiguous physical observation: '+label)
        alias,node=sorted(candidates)[0];observations[label]=node
        mapping[label]=dict(cpp_net=logical,pcell_terminal=alias,extracted_node=node)
    (work/'full.spice').write_text(text)
    text,reduction=reduce(text,g['ports'],observations.values());(work/'adc_sar_slice.extracted.spice').write_text(text)
    result=dict(junction=junction,resistor_body=body,reduction=reduction,observations=mapping)
    result['lowering']=lower(text,pdk,work/'native',temp,corner,observations)
    result['source_sha256']={str(p.resolve()):digest(p) for p in (Path(__file__),placement/'placement.json',placement/'adc_sar_slice.gds',ext/'conserved.spice',ext/'named/capacitance.spice',ext/'named/ExtractedAnalog.ext',ext/'named/ExtractedAnalog.res.ext')}
    (work/'backannotation.json').write_text(json.dumps(result,indent=2)+'\n')
    return result

if __name__=='__main__':
    p=argparse.ArgumentParser(description=__doc__)
    for name in ('placement','physical','work','pdk'):p.add_argument('--'+name,type=Path,required=True)
    a=p.parse_args();r=bind(*(getattr(a,n).resolve() for n in ('placement','physical','work','pdk')))
    print(json.dumps(dict(junctions=r['junction']['devices'],reduction=r['reduction'],counts=r['lowering']['counts']),indent=2))
