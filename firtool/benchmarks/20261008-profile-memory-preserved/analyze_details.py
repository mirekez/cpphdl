#!/usr/bin/env python3
"""Conservative hardware grouping and generated temporary liveness."""
import collections
import json
import pathlib
import re

out = pathlib.Path(__file__).resolve().parent
results = json.loads((out / 'results.json').read_text())
model = pathlib.Path(results[0]['binary']).parents[1] / 'model.h'
profile = json.loads((out / 'native-graph-profile.json').read_text())

def category(scope):
    for marker, label in [
        ('.inst_monitor', 'TileLink monitors'),
        ('.inst_fpuOpt', 'FPU'),
        ('.inst_core.inst_alu', 'Integer ALU'),
        ('.inst_dcache', 'Data cache'),
        ('.inst_frontend', 'Instruction frontend / TLB'),
        ('.inst_tlDM', 'Debug module'),
        ('.inst_bootrom', 'Boot ROM'),
        ('.inst_core', 'Other core'),
    ]:
        if marker in scope:
            return label
    return 'Other hardware'

groups = collections.Counter()
for row in profile['functions']:
    if 'graph_chunk' in row:
        categories = {category(scope) for scope, count in row['graph_chunk']['scopes']}
        key = next(iter(categories)) if len(categories) == 1 else 'Mixed hardware chunks'
    elif 'evaluate<' in row['symbol']:
        key = 'State/output/dispatch self'
    elif row['symbol'] == 'main':
        key = 'Runner main self'
    else:
        key = 'Other functions / libraries'
    groups[key] += row['samples']
(out / 'hardware-groups.json').write_text(json.dumps({
    'samples': profile['total'],
    'groups': [dict(group=k, samples=v, percent=100*v/profile['total'])
               for k, v in groups.most_common()],
}, indent=2) + '\n')

definitions = {}
uses = collections.defaultdict(set)
chunks = set()
context = None
slots = None
for line in model.read_text().splitlines():
    match = re.search(r'std::array<uint64_t,\s*(\d+)> __cpphdl_values', line)
    if match:
        slots = int(match[1])
    match = re.search(r'void __cpphdl_chunk_(\d+)\(', line)
    if match:
        context = int(match[1])
        chunks.add(context)
    if 'void evaluate()' in line:
        context = 'evaluate'
    match = re.match(r'__cpphdl_values\[(\d+)\] =', line)
    if match:
        definitions[int(match[1])] = context
        line = line[match.end():]
    for value in re.findall(r'__cpphdl_values\[(\d+)\]', line):
        uses[int(value)].add(context)
local = sum(uses[i] == {chunk} for i, chunk in definitions.items())
unused = sum(not uses[i] for i in definitions)
assert slots is not None
(out / 'temporary-storage.json').write_text(json.dumps(dict(
    emitted_nodes=len(definitions), only_consumed_in_same_chunk=local,
    cross_chunk_or_evaluate=len(definitions)-local-unused, unused_values=unused,
    persistent_array_slots=slots, persistent_array_bytes=slots*8, chunks=len(chunks),
), indent=2) + '\n')
