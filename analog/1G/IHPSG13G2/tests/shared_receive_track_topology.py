"""Exhaustive switch-network check of exported TRACK request gates.

This checks Boolean topology, not analog timing or transistor sizing. It reads
the actual exported channel terminal graph instead of duplicating C++ wiring.
"""
from collections import Counter, defaultdict
from hashlib import sha256
from itertools import product
from pathlib import Path
import json
import re
import sys


def inspect(model, deck):
    groups = defaultdict(list)
    owner = file = None
    for line in model.splitlines():
        match = re.search(r'// device (\d+), IC ([^,]+), .*/([^/]+[.]hpp):\d+', line)
        if match:
            owner, file = match.groups()[1:]
        match = re.search(r'I\((n\d+), (n\d+)\) <\+ cpphdl_channel\(V\((n\d+), (n\d+)\), V\((n\d+), (n\d+)\), ([-+0-9.eE]+), ([-+0-9.eE]+), ([-+0-9.eE]+)\);', line)
        if match:
            a, b, gate, source, drain, drain_source, beta, threshold, polarity = match.groups()
            if (source, drain, drain_source) != (b, a, b):
                raise ValueError('changed channel terminal convention')
            groups[owner, file].append((a, b, gate, float(beta), float(threshold), int(float(polarity))))
    expected = Counter({('inverter.hpp', 2): 66, ('cmos_logic.hpp', 4): 27,
        ('cascoded_level_shifter.hpp', 6): 17, ('cascoded_level_shifter.hpp', 1): 1,
        ('shared_receive_track.hpp', 12): 13, ('shared_receive_track.hpp', 8): 13,
        ('mos_track_hold.hpp', 4): 13})
    if Counter((file, len(parts)) for (_, file), parts in groups.items()) != expected:
        raise ValueError('changed shared TRACK component inventory')
    obs = dict(re.findall(r'^let observation(\d+) = v\((n\d+)\)$', deck, re.M))
    if set(obs) != set(map(str, range(169))):
        raise ValueError('changed observation interface')
    vdd, ground = obs['1'], 'n0'
    tested = 0
    for (_, file), parts in groups.items():
        if file != 'shared_receive_track.hpp':
            continue
        is_set = len(parts) == 12
        inputs = [part[2] for part in parts[:6 if is_set else 4]]
        if len(set(inputs)) != len(inputs):
            raise ValueError('aliased request gate inputs')
        output = parts[0][0] if is_set else parts[1][0]
        if output not in {obs[str(6 + 7 * i + (5 if is_set else 6))] for i in range(13)}:
            raise ValueError('request output disconnected from probe')
        if any(part[3:5] != (.008, .7) for part in parts):
            raise ValueError('changed request transistor parameters')
        for values in product((False, True), repeat=len(inputs)):
            bits = dict(zip(inputs, values))
            graph = defaultdict(set)
            for a, b, gate, _, _, sign in parts:
                if bits[gate] == (sign > 0):
                    graph[a].add(b)
                    graph[b].add(a)
            reached, pending = set(), [output]
            while pending:
                node = pending.pop()
                if node not in reached:
                    reached.add(node)
                    pending.extend(graph[node] - reached)
            if is_set:
                enable, precharge, strobe, opening, close_n, allow = values
                high = not (enable and (precharge or strobe and opening and close_n and allow))
            else:
                disable, strobe, closing, precharge_n = values
                high = not (disable or strobe and closing and precharge_n)
            if (vdd in reached, ground in reached) != (high, not high):
                raise ValueError('request gate float, contention or wrong truth table')
            tested += 1
    # Verify the qualified balanced sampler channel strengths and actual loads.
    for (_, file), parts in groups.items():
        if file == 'mos_track_hold.hpp':
            if [p[3:] for p in parts] != [(.03, .7, 1), (.015, .7, -1),
                    (.016, .7, 1), (.0084, .7, -1)]:
                raise ValueError('sampler profile changed')
    caps = [re.fullmatch(r'Cdevice(\d+) (\S+) (\S+) ([-+0-9.eE]+) IC=([-+0-9.eE]+)', line)
            for line in deck.splitlines() if line.startswith('Cdevice')]
    if len(caps) != 689 or any(m is None or float(m[5]) != 0 for m in caps):
        raise ValueError('changed or non-cold explicit storage')
    for i in range(13):
        held = obs[str(6 + 7 * i + 3)]
        if len([m for m in caps if m[2] == held and m[3] == '0' and float(m[4]) == 1.5e-12]) != 1:
            raise ValueError('missing exact sampler storage')
    return dict(status='pass', request_truth_table_cases=tested, mos_devices=655,
                capacitors=689, scope='static request-gate topology and sampler profile only; no timing qualification')


if __name__ == '__main__':
    case = Path(sys.argv[1])
    model, deck = case / 'design.va', case / 'bench.cir'
    result = inspect(model.read_text(), deck.read_text())
    result['sha256'] = {str(p): sha256(p.read_bytes()).hexdigest() for p in (model, deck, Path(__file__))}
    print(json.dumps(result, indent=2))
