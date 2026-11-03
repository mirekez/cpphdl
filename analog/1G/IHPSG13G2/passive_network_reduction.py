"""Eliminate unloaded resistor nodes while retaining specified terminals.

This operates on parsed edges, independent of device-model inventory. Every
capacitor endpoint and device terminal must be supplied as a retained node.
"""
from collections import defaultdict
import heapq
import math


def reduce_resistors(edges, retained):
    graph = defaultdict(dict)
    retained = set(retained)

    def join(a, b, resistance):
        if a == b or not math.isfinite(resistance) or resistance <= 0:
            raise ValueError('invalid resistor edge')
        previous = graph[a].get(b)
        if previous is not None:
            small, large = sorted((previous, resistance))
            resistance = small / (1 + small / large)
        graph[a][b] = graph[b][a] = resistance

    for a, b, resistance in edges:
        join(a, b, resistance)
    pending = [(len(graph[n]), n) for n in set(graph) - retained]
    heapq.heapify(pending)
    eliminated = 0
    while pending:
        degree, node = heapq.heappop(pending)
        if node not in graph or node in retained or len(graph[node]) != degree or degree > 8:
            continue
        neighbors = list(graph.pop(node).items())
        for other, _ in neighbors:
            del graph[other][node]
        if degree == 2:
            (a, ra), (b, rb) = neighbors
            join(a, b, ra + rb)
        elif degree > 2:
            conductance = sum(1 / resistance for _, resistance in neighbors)
            # Schur complement of an unloaded node, not an RC approximation.
            for i, (a, ra) in enumerate(neighbors):
                for b, rb in neighbors[i + 1:]:
                    join(a, b, ra * rb * conductance)
        eliminated += 1
        for other, _ in neighbors:
            if other not in retained:
                heapq.heappush(pending, (len(graph[other]), other))
    reduced = [(a, b, resistance) for a in sorted(graph)
               for b, resistance in sorted(graph[a].items()) if a < b]
    return reduced, dict(original_resistors=len(edges), reduced_resistors=len(reduced),
        eliminated_unloaded_nodes=eliminated, retained_nodes=len(retained),
        scope='exact resistor-only nodal elimination; no device or storage terminal removed')
