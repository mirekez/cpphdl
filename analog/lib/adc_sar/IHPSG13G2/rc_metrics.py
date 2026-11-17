#!/usr/bin/env python3
"""Effective DC wire resistance between named physical terminals.

Only the extracted linear wire resistors participate. Compact devices and
capacitors are open circuits in this measurement. It is not an EM or AC test.
"""
import argparse,json,re,shlex
from collections import defaultdict
from pathlib import Path
import numpy as np

def effective(resistors,reference,targets):
    graph=defaultdict(list)
    for a,b,r in resistors:
        if not np.isfinite(r) or r<=0:raise ValueError('nonphysical wire resistance')
        if a==b:continue
        graph[a].append((b,r));graph[b].append((a,r))
    connected={reference};pending=[reference]
    while pending:
        for b,_ in graph[pending.pop()]:
            if b not in connected:connected.add(b);pending.append(b)
    if not set(targets)<=connected:raise ValueError('disconnected wire terminal')
    names=sorted(connected-{reference});index={n:i for i,n in enumerate(names)}
    matrix=np.zeros((len(names),len(names)));rhs=np.zeros((len(names),len(targets)))
    for a,b,r in resistors:
        if a not in connected or a==b:continue
        if a!=reference:matrix[index[a],index[a]]+=1/r
        if b!=reference:matrix[index[b],index[b]]+=1/r
        if a!=reference and b!=reference:
            matrix[index[a],index[b]]-=1/r;matrix[index[b],index[a]]-=1/r
    for j,n in enumerate(targets):
        if n!=reference:rhs[index[n],j]=1
    volts=np.linalg.solve(matrix,rhs) if names else rhs
    residual=float(np.max(np.abs(matrix@volts-rhs))) if names else 0
    if residual>1e-6:raise ValueError('wire solve residual exceeds 1 uA for 1 A excitation')
    values=[float(volts[index[n],j]) if n!=reference else 0. for j,n in enumerate(targets)]
    if any(v<0 or not np.isfinite(v) for v in values):raise ValueError('invalid effective resistance')
    return dict(resistance_Ohm=values,nodes=len(connected),maximum_residual_A=residual)

def run(circuit,ext,placement):
    parent={}
    def root(n):
        parent.setdefault(n,n)
        if parent[n]!=n:parent[n]=root(parent[n])
        return parent[n]
    resistors=[]
    for line in circuit.read_text().splitlines():
        f=line.split()
        if f and f[0]=='R':
            resistors.append((f[2],f[3],float(f[4])));parent[root(f[3])]=root(f[2])
    # Logical .ext aliases collapse the entire conductor, including its wire
    # resistance. Locate the actual device terminals in .res.ext instead.
    raw=ext.read_text();scale=float(re.findall(r'^scale \S+ \S+ (\S+)$',raw,re.M)[0])*.01
    devices=[shlex.split(line) for line in raw.splitlines() if line.startswith('device ')]
    geometry={p['name']:p for p in json.loads(placement.read_text())['components']}
    def terminals(alias,port):
        name,pin=alias.split('_0.');part=geometry[name];g=part['geometry'];x,y=part['origin_um']
        if g.get('configuration')=='parallel':
            # A parallel storage macro has two genuine MIM plate anchors.
            # Match both PCell origins explicitly; a zero-size fallback bbox
            # selected one or zero depending on float rounding at the grid.
            pitch=g['pitch_um'];centers=(x,x+pitch);tolerance=.005
            selected=[d for d in devices if d[2]=='cap_cmim'
                      and abs(float(d[4])*scale-y)<=tolerance
                      and any(abs(float(d[3])*scale-c)<=tolerance for c in centers)]
            expected=g['capacitors']
        else:
            bounds=g.get('bbox_um',[0,0,g.get('width_um',0),g.get('length_um',0)])
            selected=[d for d in devices if x+bounds[0]<=float(d[3])*scale<=x+bounds[2] and y+bounds[1]<=float(d[4])*scale<=y+bounds[3]]
            expected=g['fingers'] if part['kind']=='mos' else 1
        if len(selected)!=expected:raise ValueError('ambiguous physical device anchors: '+name)
        indices={'S':(11,14),'G':(8,),'B':(7,),'MINUS':(8,11)}[pin]
        nodes=sorted({d[i] for d in selected for i in indices if root(d[i])==root(port)})
        if not nodes or port in nodes:raise ValueError('missing distributed physical terminals: '+alias)
        return nodes
    groups={'VSS':['D3_0.MINUS','D4_0.S','D0_0.B'],
            'VDD':['D5_0.S','D1_0.B'], 'IN':['D0_0.S','D1_0.S'],
            'TRACK':['D0_0.G','D4_0.G','D7_0.G']}
    rows=[]
    for port,aliases in groups.items():
        targets=[(a,n) for a in aliases for n in terminals(a,port)];result=effective(resistors,port,[n for _,n in targets])
        for (alias,node),r in zip(targets,result['resistance_Ohm']):
            rows.append(dict(port=port,terminal=alias,node=node,resistance_Ohm=r,
                             maximum_residual_A=result['maximum_residual_A']))
    return dict(scope='effective DC resistance of extracted wire network; all compact devices and capacitors open; no current-density qualification',measurements=rows)

if __name__=='__main__':
    p=argparse.ArgumentParser(description=__doc__)
    for key in ('circuit','ext','placement','output'):p.add_argument('--'+key,type=Path,required=True)
    a=p.parse_args();r=run(a.circuit,a.ext,a.placement);a.output.write_text(json.dumps(r,indent=2)+'\n')
    for row in r['measurements']:print(row['port'],row['terminal'],row['resistance_Ohm'])
