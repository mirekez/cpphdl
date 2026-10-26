"""Attach Magic resistor-body couplings to explicit R3 electrical midpoints.

The R3 models supply intrinsic body-to-substrate charge. Remove Magic's
duplicate body-area/perimeter shunts, retaining its external coupling and
wire capacitance. This model partition still requires independent field and
contact-head calibration; passing algebra checks is not circuit qualification.
"""
from collections import defaultdict
import math
import re

from extract_analog import inventory, number
from extracted_connections import check_capacitor_connections
from resistor_body_model import split_body


def adapt(reference_text, distributed_text, ext_text, ports, substrate='VSS'):
    reference = inventory(reference_text, ports)
    distributed = inventory(distributed_text, ports)
    parent = {}

    def root(node):
        parent.setdefault(node, node)
        if parent[node] != node:
            parent[node] = root(parent[node])
        return parent[node]

    for a, b in re.findall(r'^equiv "([^"]+)" "([^"]+)"', ext_text, re.M):
        parent[root(b)] = root(a)
    scale = re.findall(r'^scale (\S+) (\S+) (\S+)$', ext_text, re.M)
    if len(scale) != 1 or float(scale[0][1]) != 1:
        raise ValueError('requires explicit one-attofarad EXT capacitance units')
    charges = {name: float(value) * 1e-18 for name, value in
               re.findall(r'^node "([^"]+)" \S+ (\S+) ', ext_text, re.M)}
    bodies = {}
    for line in ext_text.splitlines():
        if not line.startswith('device rsubckt '):
            continue
        fields = re.findall(r'"([^"]+)"', line)
        model = line.split()[2]
        if model not in ('rppd', 'rhigh') or len(fields) != 4:
            raise ValueError('unsupported extracted resistor body')
        bulk, body, a, b = fields
        key = (model, *sorted((root(a), root(b))), root(bulk))
        if key in bodies or body not in charges or not math.isfinite(charges[body]) or charges[body] <= 0:
            raise ValueError('missing or ambiguous resistor-body capacitance')
        bodies[key] = dict(body=body, capacitance_F=charges[body], model=model, terminals=[a, b], substrate=bulk)
    if not bodies:
        raise ValueError('no supported resistor-body extraction')

    def devices(text):
        result = {}
        for line in text.splitlines():
            fields = line.split(';')[0].split()
            if fields and fields[0].startswith('X'):
                index = next(i for i, token in enumerate(fields) if token in reference['models'])
                result[fields[0]] = (fields[1:index], fields[index], dict(v.split('=') for v in fields[index + 1:]))
        return result

    originals, routed = devices(reference_text), devices(distributed_text)
    assignments, removal = {}, defaultdict(float)
    used = set()
    for name, (nodes, model, parameters) in originals.items():
        if model not in ('rppd', 'rhigh'):
            continue
        a, b, bulk = nodes
        key = (model, *sorted((root(a), root(b))), root(bulk))
        if key not in bodies or key in used or root(bulk) != root(substrate):
            raise ValueError('resistor body does not match its independent SPICE terminals')
        used.add(key)
        body = bodies[key]
        routed_nodes, routed_model, routed_parameters = routed[name]
        if routed_model != model or routed_parameters != parameters:
            raise ValueError('distributed extraction changed resistor geometry')
        if set(parameters) != {'w', 'l'}:
            raise ValueError('unreviewed resistor parameters')
        replacement, geometry = split_body(name + '_body', routed_nodes, body['body'], model,
                                           number(parameters['w']), number(parameters['l']))
        assignments[name] = (replacement, dict(body, representation=geometry))
        for node in (a, b):
            if root(node) != root(substrate):
                removal[node] += body['capacitance_F'] / 2
    if used != set(bodies):
        raise ValueError('unmatched resistor-body extraction')

    # Resolve the actual distributed conductor network to its original nets.
    wire_parent = {}

    def wire_root(node):
        wire_parent.setdefault(node, node)
        if wire_parent[node] != node:
            wire_parent[node] = wire_root(wire_parent[node])
        return wire_parent[node]

    for a, b, _ in distributed['resistors']:
        wire_parent[wire_root(a)] = wire_root(b)
    original_nodes = {n for a, b, _ in reference['capacitors'] for n in (a, b)} | set(ports)
    names = defaultdict(set)
    for node in original_nodes:
        names[wire_root(node)].add(node)
    if any(len(group) != 1 for group in names.values()):
        raise ValueError('wire extraction merges distinct reference nodes')

    def logical(node):
        candidates = names.get(wire_root(node), set())
        if len(candidates) != 1:
            raise ValueError('unmapped distributed capacitance node: ' + node)
        return next(iter(candidates))

    substrate_cap = defaultdict(float)
    for a, b, value in distributed['capacitors']:
        a, b = logical(a), logical(b)
        if a != b and substrate in (a, b):
            substrate_cap[b if a == substrate else a] += value
    ratios, rounding = {}, {}
    for node, value in removal.items():
        total = substrate_cap[node]
        # The pinned Magic formatter rounds to five decimal places in fF.
        # Record a deficit of at most half that output quantum separately;
        # larger discrepancies remain a charge-conservation failure.
        # The subtraction itself can land one floating-point step above the
        # exact decimal half-quantum; include only that arithmetic roundoff.
        if total > 0 and 0 < value - total <= 5e-21 + 4 * math.ulp(value):
            rounding[node] = value - total
            removal[node] = value = total
        if total < value or not math.isfinite(total):
            raise ValueError('body capacitance exceeds extracted substrate load: ' + node)
        ratios[node] = (total - value) / total
    output, cap_index = [], 0
    for line in distributed_text.splitlines():
        fields = line.split(';')[0].split()
        if fields and fields[0] in assignments:
            output.extend(assignments[fields[0]][0].splitlines())
            continue
        if fields and fields[0].startswith('C'):
            a, b, value = distributed['capacitors'][cap_index]
            cap_index += 1
            if fields[1:3] != [a, b]:
                raise ValueError('changed capacitance record ordering')
            a, b = logical(a), logical(b)
            if a != b and substrate in (a, b):
                value *= ratios.get(b if a == substrate else a, 1.)
                line = ' '.join(fields[:3]) + f' {value:.17g}'
        output.append(line)
    text = '\n'.join(output) + '\n'
    connections = check_capacitor_connections(text, ports)
    final = inventory(text, ports)
    removed = sum(v for a, b, v in distributed['capacitors']) - sum(v for a, b, v in final['capacitors'])
    if not math.isclose(removed, sum(removal.values()), rel_tol=1e-10, abs_tol=1e-24):
        raise ValueError('resistor-body capacitance partition did not conserve its declared removal')
    return text, dict(physical_resistors=len(assignments), electrical_segments=2 * len(assignments),
        bodies={name: data for name, (_, data) in assignments.items()}, connections=connections,
        removed_duplicate_body_substrate_F=removed, substrate_load_scale=ratios,
        output_rounding_F=rounding,
        scope='R3 intrinsic body charge replaces Magic resistor-body substrate shunts; '
              'external coupling retained at finite-resistance midpoints; contact-head/field calibration pending')
