"""Attest the first 70 ns of a completed five-input native campaign."""
import argparse,csv,json
from pathlib import Path
from measure import measure
from provenance import digest,verify

def run(source,output):
    if output.exists() and any(output.iterdir()):raise ValueError('prefix directory must be empty')
    checks=json.loads((source/'checks.json').read_text())
    if checks['status'] not in ('PASS','FAIL') or not checks['native']['conversion_complete']:
        raise ValueError('source campaign is incomplete')
    verify(json.loads((source/'inputs.json').read_text()))
    manifest=json.loads((source/'manifest.json').read_text());verify(manifest['artifact_sha256'])
    if len(checks['measurements']['samples'])!=5 or checks['measurements']['samples'][0]['input_V']!=.85:
        raise ValueError('expected first 0.85 V conversion in a five-input campaign')
    output.mkdir(parents=True,exist_ok=True)
    def subset(name,select):
        with (source/name).open() as stream:
            reader=csv.DictReader(stream);fields=reader.fieldnames;rows=[r for r in reader if select(r)]
        if not rows:raise ValueError('empty '+name+' prefix')
        with (output/name).open('w') as stream:
            writer=csv.DictWriter(stream,fieldnames=fields);writer.writeheader();writer.writerows(rows)
        return rows
    trace=subset('trace.csv',lambda r:float(r['time_s'])<=70e-9+1e-18)
    if abs(float(trace[-1]['time_s'])-70e-9)>1e-18:raise ValueError('missing exact 70 ns boundary')
    result=subset('results.csv',lambda r:r['sample']=='0')
    decisions=subset('decisions.csv',lambda r:r['sample']=='0')
    if len(result)!=1 or len(decisions)!=8:raise ValueError('incomplete first conversion')
    subset('controls.csv',lambda r:float(r['time_s'])<70e-9)
    measured=measure(output)
    original=checks['measurements']['samples'][0]
    for key in ['code','ideal','code_error','acquisition_V','hold_pp_V','invalid_decisions']:
        if abs(measured['samples'][0][key]-original[key])>1e-12:
            raise ValueError('prefix changed measured conversion: '+key)
    (output/'prefix-receipt.json').write_text(json.dumps(dict(status='PASS',qualified=False,
        scope='first 70 ns of the exact completed five-input electrical trace; no other input levels claimed',
        source_sha256={str(p.resolve()):digest(p) for p in [source/'manifest.json',source/'trace.csv',source/'results.csv',source/'decisions.csv',Path(__file__)]},
        artifact_sha256={str(p.resolve()):digest(p) for p in output.glob('*.csv')}),indent=2)+'\n')
    return measured

if __name__=='__main__':
    p=argparse.ArgumentParser(description=__doc__);p.add_argument('source',type=Path);p.add_argument('output',type=Path)
    a=p.parse_args();print(run(a.source,a.output)['status'])
