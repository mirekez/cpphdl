"""Exact nodal elimination of unloaded extracted resistor junctions.

Every port, device terminal and capacitor endpoint remains. No capacitance,
device or RC time constant is approximated. Resistor-only junctions are
eliminated by Kirchhoff's laws; degree is bounded to limit matrix fill.
"""
from collections import defaultdict
import heapq
import math

from extract_analog import inventory


def reduce(text, ports, observations=()):
    original = inventory(text, ports)
    protected = set(ports) | set(observations)
    graph = defaultdict(dict)
    retained = []
    for line in text.splitlines():
        fields = line.split(';')[0].split()
        if fields and fields[0].startswith('R'):
            continue
        retained.append(line)
        if fields and fields[0].startswith('C'):
            protected.update(fields[1:3])
        elif fields and fields[0].startswith('X'):
            model = next(i for i, token in enumerate(fields) if token in original['models'])
            protected.update(fields[1:model])

    def join(a, b, resistance):
        previous = graph[a].get(b)
        if previous is not None:
            # Stable parallel formula avoids multiplying very large values.
            small, large = sorted((previous, resistance))
            resistance = small / (1 + small / large)
        if not math.isfinite(resistance) or resistance <= 0:
            raise ValueError('invalid reduced resistance')
        graph[a][b] = graph[b][a] = resistance

    for a, b, value in original['resistors']:
        join(a, b, value)
    missing = set(observations) - set(graph) - {n for row in original['capacitors'] for n in row[:2]}
    if missing:
        raise ValueError('unknown observation nodes: ' + str(sorted(missing)))
    pending = [(len(graph[n]), n) for n in set(graph) - protected]
    heapq.heapify(pending)
    dead_ends = series = stars = 0
    while pending:
        degree, node = heapq.heappop(pending)
        if node not in graph or node in protected or len(graph[node]) != degree or degree > 8:
            continue
        neighbors = list(graph.pop(node).items())
        for other, _ in neighbors:
            del graph[other][node]
        if len(neighbors) == 2:
            (a, ra), (b, rb) = neighbors
            join(a, b, ra + rb)
            series += 1
        elif len(neighbors) > 2:
            conductance = sum(1 / resistance for _, resistance in neighbors)
            # Schur complement of this unloaded node: g_ab = g_a*g_b/sum(g).
            for i, (a, ra) in enumerate(neighbors):
                for b, rb in neighbors[i + 1:]:
                    join(a, b, ra * rb * conductance)
            stars += 1
        else:
            dead_ends += 1
        for other, _ in neighbors:
            if other not in protected:
                heapq.heappush(pending, (len(graph[other]), other))
    resistors = [(a, b, value) for a in sorted(graph) for b, value in sorted(graph[a].items()) if a < b]
    lines = [f'Rwire{i} {a} {b} {value:.17g}' for i, (a, b, value) in enumerate(resistors)]
    end = next(i for i, line in enumerate(retained) if line.strip().lower() == '.ends')
    retained[end:end] = lines
    output = '\n'.join(retained) + '\n'
    reduced = inventory(output, ports)
    if (original['models'] != reduced['models'] or original['geometry'] != reduced['geometry']
            or original['capacitors'] != reduced['capacitors']):
        raise ValueError('wire reduction altered a device or capacitor')
    return output, dict(original_resistors=len(original['resistors']), reduced_resistors=len(resistors),
        eliminated_series_junctions=series, eliminated_dead_ends=dead_ends, eliminated_star_junctions=stars,
        preserved_capacitors=len(original['capacitors']), protected_nodes=len(protected),
        scope='exact resistor-only nodal elimination; all energy-storage and device terminals retained')
