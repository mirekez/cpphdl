"""Content-addressed evidence; a dirty checkout is permitted, stale evidence is not."""
import hashlib,json,subprocess
from pathlib import Path
ROOT=Path(__file__).resolve().parents[4]
def digest(path):return hashlib.sha256(Path(path).read_bytes()).hexdigest()
def snapshot(pdk,extra=()):
    files=set(Path(p).resolve() for p in extra)
    files.add((pdk/'ihp-sg13g2/libs.doc/doc/SG13G2_os_process_spec.pdf').resolve())
    directories=[ROOT/'analog/include',ROOT/'analog/basic',ROOT/'analog/lib/adc_sar',ROOT/'analog/1G/IHPSG13G2',ROOT/'analog/pdk/1G_repeater_IHPSG13G2',pdk/'ihp-sg13g2/libs.tech/ngspice',pdk/'ihp-sg13g2/libs.tech/klayout']
    suffixes={'.h','.hpp','.cpp','.py','.lib','.osdi','.drc','.rb','.lym','.lyt','.lyp','.json','.cir'}
    for directory in directories:
        files.update(p.resolve() for p in directory.rglob('*') if p.is_file() and p.suffix in suffixes and '__pycache__' not in p.parts and 'results' not in p.parts and p.name not in ('checks.json','manifest.json'))
    return {str(p):digest(p) for p in sorted(files)}
def verify(hashes):
    changed=[p for p,h in hashes.items() if not Path(p).is_file() or digest(p)!=h]
    if changed:raise ValueError('evidence inputs changed: '+', '.join(changed[:8]))
def receipt(path,inputs,artifacts,**data):
    verify(inputs);data.update(input_sha256=inputs,artifact_sha256={str(Path(p).resolve()):digest(p) for p in artifacts})
    Path(path).write_text(json.dumps(data,indent=2)+'\n');return data
