"""Inventory explicit capacitance on distributed physical signal conductors.

This is a diagnostic sum, not an AC load model: coupling remains coupling in
the simulation, and voltage-dependent PSP intrinsic capacitance is excluded.
"""
import argparse,json,math,re,shlex
from pathlib import Path
from provenance import digest

def inventory(circuit,aliases,connections,selected):
    parent={}
    def root(n):
        parent.setdefault(n,n)
        if parent[n]!=n:parent[n]=root(parent[n])
        return parent[n]
    def join(a,b):parent[root(b)]=root(a)
    for a,b in aliases:join(a,b)
    records=[shlex.split(l) for l in circuit.splitlines() if l.strip() and not l.startswith('#')]
    for f in records:
        if f[0]=='R':
            if not math.isfinite(float(f[4])) or float(f[4])<=0:raise ValueError('invalid conductor resistance')
            join(f[2],f[3])
    names={}
    for node,logical in connections.items():
        key=root(node)
        if key in names and names[key]!=logical:raise ValueError('distinct logical nets joined by conductor network')
        names[key]=logical
    if not set(selected)<=set(names.values()):raise ValueError('missing selected net')
    result={n:{} for n in selected}
    for f in records:
        if f[0]!='C':continue
        value=float(f[4])
        if not math.isfinite(value) or value<0:raise ValueError('invalid capacitance')
        a,b=root(f[2]),root(f[3])
        if a==b:continue # Within-wire capacitance has no net incident load.
        a,b=names.get(a,a),names.get(b,b)
        for net,other in [(a,b),(b,a)]:
            if net in result:result[net][other]=result[net].get(other,0)+value
    return {n:dict(explicit_incident_F=sum(c.values()),couplings_F=dict(sorted(c.items(),key=lambda x:-x[1]))) for n,c in result.items()}

def run(circuit,ext,placement):
    g=json.loads(placement.read_text());parts={p['name']:p for p in g['components']}
    selected=[parts[n]['connections']['D'] for n in ['D64','D65','D78','D79','D89','D90']]
    connections={p['name']+'_0.'+pin:net for p in g['components'] for pin,net in p['connections'].items()}
    aliases=re.findall(r'^equiv "([^"]+)" "([^"]+)"',ext.read_text(),re.M)
    if not aliases:raise ValueError('named unresisted .ext aliases required')
    return dict(scope=__doc__,source_sha256={str(p.resolve()):digest(p) for p in (Path(__file__),circuit,ext,placement)},
                nets=inventory(circuit.read_text(),aliases,connections,selected))

if __name__=='__main__':
    p=argparse.ArgumentParser(description=__doc__)
    for key in ['circuit','ext','placement','output']:p.add_argument('--'+key,type=Path,required=True)
    a=p.parse_args();a.output.write_text(json.dumps(run(a.circuit,a.ext,a.placement),indent=2)+'\n')
