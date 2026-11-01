"""Preserve the physical PLL RC network while binding exported observations."""
import re

from extract_analog import inventory
from extracted_connections import check_capacitor_connections
from reduce_extracted_wires import reduce
from reference_clock_layout_process import PORTS, netlist as physical_devices
from resistor_body_extraction import adapt


def bind_observations(geometry, requested, ext, reference_nodes):
    parent = {}

    def root(node):
        parent.setdefault(node, node)
        if parent[node] != node:
            parent[node] = root(parent[node])
        return parent[node]

    for a, b in re.findall(r'^equiv "([^"]+)" "([^"]+)"', ext, re.M):
        parent[root(b)] = root(a)
    result, audit = {}, {}
    for name, logical in requested.items():
        if logical in PORTS:
            result[name] = logical
            continue
        terminals = sorted(part['name'] + '_0.' + terminal
            for part in geometry['components']
            for terminal, net in part['connections'].items() if net == logical)
        if not terminals or len({root(n) for n in terminals}) != 1:
            raise ValueError('missing or disconnected PLL observation: ' + name)
        candidates = sorted(n for n in reference_nodes if root(n) == root(terminals[0]))
        if len(candidates) != 1:
            raise ValueError('missing or ambiguous extracted PLL node: ' + name)
        result[name] = candidates[0]
        audit[name] = dict(circuit_net=logical, extracted_net=result[name], layout_terminals=terminals)
    # Resolve locations only. Never short distributed branches to an ideal net.
    return result, audit


def netlist(extraction, geometry, requested):
    ext = (extraction / 'ExtractedAnalog.ext').read_text()
    reference = (extraction / 'capacitance.spice').read_text()
    text = (extraction / 'conserved.spice').read_text()
    _, expected = physical_devices(geometry)
    if inventory(text, PORTS)['models'] != expected:
        raise ValueError('extracted reference-clock device inventory changed')
    reference_nodes = set(PORTS)
    for line in reference.splitlines():
        fields = line.split(';')[0].split()
        if fields and fields[0].startswith(('R', 'C')):
            reference_nodes.update(fields[1:3])
        elif fields and fields[0].startswith('X'):
            index = next(i for i, token in enumerate(fields) if token in expected)
            reference_nodes.update(fields[1:index])
    probes, bindings = bind_observations(geometry, requested, ext, reference_nodes)
    text, bodies = adapt(reference, text, ext, PORTS)
    connections = check_capacitor_connections(text, PORTS)
    text, reduction = reduce(text, PORTS, [n for n in probes.values() if n not in PORTS])
    text = re.sub(r'^(X[^\n]+\bcap_cmim\b[^\n]*)$', r'\1 ic=0', text, flags=re.M)
    text = text.replace('.subckt ExtractedAnalog ', '.subckt PhysicalReferenceClock ', 1)
    return text, probes, dict(bindings=bindings, physical_devices=expected,
        resistor_body_partition=bodies, connections=connections, reduction=reduction)
