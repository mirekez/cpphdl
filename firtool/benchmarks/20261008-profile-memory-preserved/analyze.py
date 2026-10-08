#!/usr/bin/env python3
import bisect, collections, json, pathlib, re, subprocess
out=pathlib.Path(__file__).resolve().parent
results=json.loads((out/'results.json').read_text())
graph=pathlib.Path(results[0]['binary']).parents[1]/'model.h.graph'
nodes=[]
with graph.open() as f:
    for _ in range(int(next(f))):
        line=next(f)
        op,width=re.match(r'"([^"]+)" (\d+)',line).groups()
        name=re.search(r'"([^"]*)"\s*$',line).group(1)
        nodes.append((op,int(width),name.split('/')[0]))
chunks=collections.defaultdict(list)
chunk=None
with graph.with_suffix('').open() as f:
    for line in f:
        m=re.search(r'void __cpphdl_chunk_(\d+)\(',line)
        if m: chunk=int(m[1])
        m=re.match(r'__cpphdl_values\[(\d+)\] =',line)
        if m: chunks[chunk].append(int(m[1]))
chunk_summary={}
for chunk,ids in chunks.items():
    chunk_summary[chunk]=dict(nodes=len(ids),scopes=collections.Counter(nodes[i][2] for i in ids).most_common(),
                             ops=collections.Counter(nodes[i][0] for i in ids).most_common())
(out/'graph-chunks.json').write_text(json.dumps(chunk_summary,indent=2)+'\n')
summary=[]
for result in results:
    mode=result['backend']
    syms=[]
    for line in subprocess.check_output(['nm','-n','-S','--demangle',result['binary']],text=True).splitlines():
        m=re.match(r'([0-9a-f]+) ([0-9a-f]+) [tTwW] (.*)',line)
        if m: syms.append((int(m[1],16),int(m[2],16),m[3]))
    syms.sort(); starts=[s[0] for s in syms]
    lines=(out/(mode+'.samples')).read_text().splitlines()
    total=int(re.search(r'total (\d+)',lines[0])[1])
    outside=int(re.search(r'outside_executable (\d+)',lines[0])[1])
    counts=collections.Counter(); pcs=[]
    for line in lines[1:]:
        pc,count=line.split(); pc=int(pc,16); count=int(count)
        i=bisect.bisect_right(starts,pc)-1
        sym=syms[i] if i>=0 and pc < syms[i][0]+syms[i][1] else (0,0,'[unknown]')
        counts[sym[2]]+=count
        pcs.append(dict(pc=hex(pc),samples=count,symbol=sym[2],offset=pc-sym[0]))
    counts['[outside executable]']+=outside
    functions=[]
    for name,count in counts.most_common():
        row=dict(symbol=name,samples=count,percent=100*count/total)
        match=re.search(r'__cpphdl_chunk_(\d+)\(',name)
        if match: row['graph_chunk']=chunk_summary[int(match[1])]
        functions.append(row)
    report=dict(backend=mode,total=total,functions=functions,pcs=sorted(pcs,key=lambda x:-x['samples']))
    (out/(mode+'-profile.json')).write_text(json.dumps(report,indent=2)+'\n')
    if mode=='native-graph':
        groups=collections.Counter(); mixed=[]
        for row in functions:
            c=row.get('graph_chunk'); name=row['symbol']
            if c:
                rom=sum(n for scope,n in c['scopes'] if scope.endswith('.inst_bootrom'))
                if rom==c['nodes']: key='Pure boot ROM chunks'
                elif rom:
                    key='Mixed boot ROM chunks'
                    mixed.append((name,rom,c['nodes'],row['percent']))
                else: key='Other graph chunks'
            elif 'evaluate<' in name: key='State commit / outputs / dispatch (self)'
            elif name=='main': key='Runner (self)'
            else: key='Other functions / libraries'
            groups[key]+=row['samples']
        (out/'native-graph-groups.json').write_text(json.dumps(
            dict(total=total,groups=groups,mixed_rom_chunks=mixed),indent=2)+'\n')
    with (out/(mode+'-functions.tsv')).open('w') as f:
        f.write('percent\tsamples\tsymbol\n')
        for row in functions: f.write(f"{row['percent']:.3f}\t{row['samples']}\t{row['symbol']}\n")
    print(mode,'samples',total)
    for row in functions[:16]:
        print(f"{row['percent']:6.2f}%",row['symbol'],row.get('graph_chunk',{}).get('scopes',[])[:2])
    summary.append(dict(backend=mode,total_samples=total,seconds=result['seconds'],passed=result['passed']))
(out/'profile-summary.json').write_text(json.dumps(summary,indent=2)+'\n')
