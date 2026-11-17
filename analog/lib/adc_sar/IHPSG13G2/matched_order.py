"""Place coupled and differential device pairs as indivisible row groups.

Independent wire-length minimization separated the comparator's two isolation
buffers by hundreds of microns. Its extracted output loads differed by ~30 fF,
and adding 30 fF to the lighter output corrected an otherwise wrong decision.
Keep paired devices/loads adjacent and penalize differential output-span error.
The geometric objective is a placement heuristic, never electrical acceptance.
"""
from collections import defaultdict
import math,random

PAIRS=((0,1),(4,5),(6,7),(64,65),(66,67),(68,69),(71,72),(74,75),(76,77),
       (78,79),(81,82),(83,84),(85,87),(86,88),(89,90),(92,93),(94,95),(96,97),
       (98,102),(99,103),(100,104),(101,105),(106,116),(107,117),(108,118),(109,119),
       (110,120),(111,121),(112,122),(113,123),(114,124),(115,125))


def order(parts,dbu,weights,balanced_nets,iterations=16000,fixed_order=None):
    names={cell.name:i for i,(cell,_,_,_) in enumerate(parts)}
    if len(names)!=len(parts):raise ValueError('duplicate placement name')
    if len(balanced_nets)!=2 or len(set(balanced_nets))!=2:
        raise ValueError('two distinct comparator output nets required')
    if any(not math.isfinite(v) or v<=0 for v in weights.values()):raise ValueError('invalid net weight')
    pairs=[];paired={}
    for a,b in PAIRS:
        keys=['D'+str(a),'D'+str(b)]
        if not all(k in names for k in keys):raise ValueError('slice inventory changed: '+str(keys))
        pair=[names[k] for k in keys]
        if any(i in paired for i in pair):raise ValueError('duplicate matching constraint')
        for i in pair:paired[i]=pair
        pairs.append(keys)
    groups=[];seen=set()
    for i in range(len(parts)):
        if i in seen:continue
        group=paired.get(i,[i]);groups.append(list(group));seen.update(group)
    widths=[cell.bbox().width()*dbu+10 for cell,_,_,_ in parts]
    nets=defaultdict(set)
    for i,(_,_,connections,_) in enumerate(parts):
        for net in set(connections.values())-{'VDD','VSS'}:nets[net].add(i)
    if any(n not in nets for n in balanced_nets):raise ValueError('missing comparator output')
    def score(groups):
        locations=[0.]*len(parts);x=0.
        for group in groups:
            for i in group:locations[i]=x+widths[i]/2;x+=widths[i]
        spans={n:max(locations[i] for i in ids)-min(locations[i] for i in ids) for n,ids in nets.items()}
        wire=sum(weights.get(n,1)*v for n,v in spans.items())
        balance=abs(spans[balanced_nets[0]]-spans[balanced_nets[1]])
        return wire+30*balance,wire,balance
    if fixed_order is not None:
        if len(fixed_order)!=len(names) or set(fixed_order)!=set(names):
            raise ValueError('fixed order must contain every component exactly once')
        positions={n:i for i,n in enumerate(fixed_order)}
        if any(abs(positions[a]-positions[b])!=1 for a,b in pairs):
            raise ValueError('fixed order separates a matched pair')
        result=[names[n] for n in fixed_order];measured=score([[i] for i in result])
        return result,dict(component_order=list(fixed_order),paired_components=pairs,
            placed_weighted_span_um=measured[1],comparator_output_span_difference_um=measured[2],
            net_weights=weights,fixed_order=True,
            scope='fixed paired order for controlled sizing changes; fresh extraction/DRC/LVS/electrical tests required')
    rng=random.Random(130212);initial=score(groups);current=initial[0]
    best=[g[:] for g in groups];best_score=initial
    for iteration in range(iterations):
        trial=[g[:] for g in groups];a,b=sorted(rng.sample(range(len(groups)),2))
        if iteration%5==0:trial[a].reverse()
        elif iteration%3:trial[a],trial[b]=trial[b],trial[a]
        else:trial[a:b+1]=reversed(trial[a:b+1])
        measured=score(trial);temperature=120*(1-iteration/iterations)**3+.01
        if measured[0]<current or rng.random()<math.exp(min(0,(current-measured[0])/temperature)):
            groups,current=trial,measured[0]
            if measured[0]<best_score[0]:best,best_score=[g[:] for g in groups],measured
    result=[i for g in best for i in g]
    if sorted(result)!=list(range(len(parts))):raise ValueError('placement lost a component')
    return result,dict(component_order=[parts[i][0].name for i in result],paired_components=pairs,
                       initial_weighted_span_um=initial[1],placed_weighted_span_um=best_score[1],
                       comparator_output_span_difference_um=best_score[2],
                       net_weights=weights,seed=130212,
                       scope='paired row placement; span balance is a heuristic; extraction/DRC/LVS/electrical tests required')
