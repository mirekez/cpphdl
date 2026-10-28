"""Attach parallel resistor bodies using Magic's geometric device identities.

The capacitance and resistance extracts can enumerate identical parallel
devices differently. Match their rectangles and distributed terminals, never
their SPICE instance indices. This adapter handles resistor-only arrays.
"""
from collections import Counter, defaultdict
import math
import re
import shlex

from extract_analog import inventory, number
from extracted_connections import check_capacitor_connections
from resistor_body_model import split_body


def adapt(reference_text, distributed_text, ext_text, res_ext_text, ports, substrate):
    reference = inventory(reference_text, ports)
    distributed = inventory(distributed_text, ports)
    if set(reference['models']) - {'rppd', 'rhigh'}:
        raise ValueError('array adapter requires resistor-only devices')
    scales = re.findall(r'^scale (\S+) (\S+) (\S+)$', ext_text, re.M)
    if len(scales) != 1 or float(scales[0][1]) != 1:
        raise ValueError('requires one-attofarad EXT capacitance units')
    if re.findall(r'^scale (\S+) (\S+) (\S+)$', res_ext_text, re.M) != scales:
        raise ValueError('distributed EXT scale changed')
    length_scale = float(scales[0][2]) * 1e-8
    charges = {name: float(value) * 1e-18 for name, value in
               re.findall(r'^node "([^"]+)" \S+ (\S+) ', ext_text, re.M)}
    bodies, terminals = {}, {}
    for text, routed in ((ext_text, False), (res_ext_text, True)):
        seen = set()
        for line in text.splitlines():
            if not line.startswith('device rsubckt '):
                continue
            fields = shlex.split(line)
            key = tuple(fields[2:7])  # Model and immutable device rectangle.
            quoted = re.findall(r'"([^"]+)"', line)
            if fields[2] not in ('rppd', 'rhigh') or len(quoted) != 4 or key in seen:
                raise ValueError('unsupported or duplicate resistor identity')
            seen.add(key)
            bulk, body, a, b = quoted
            if not routed:
                dimensions = dict(item.split('=') for item in fields[7:9])
                charge = charges.get(body, 0.)
                if set(dimensions) != {'w', 'l'} or not math.isfinite(charge) or charge <= 0:
                    raise ValueError('missing resistor geometry or body charge')
                bodies[key] = dict(body=body, capacitance_F=charge, model=fields[2],
                    terminals=[a, b], substrate=bulk,
                    width_m=float(dimensions['w']) * length_scale,
                    length_m=float(dimensions['l']) * length_scale)
            else:
                if key not in bodies or bodies[key]['body'] != body:
                    raise ValueError('distributed resistor identity changed')
                terminal_key = (fields[2], *sorted((a, b)), bulk)
                if terminal_key in terminals:
                    raise ValueError('ambiguous distributed resistor terminals')
                terminals[terminal_key] = key
        if routed and seen != set(bodies):
            raise ValueError('missing distributed resistor identity')
    if not bodies:
        raise ValueError('empty resistor array')

    parent = {}

    def root(node):
        parent.setdefault(node, node)
        if parent[node] != node:
            parent[node] = root(parent[node])
        return parent[node]

    for a, b, _ in distributed['resistors']:
        parent[root(a)] = root(b)
    logical_names = set(ports)
    for a, b, _ in reference['capacitors']:
        logical_names.update((a, b))
    groups = defaultdict(set)
    for node in logical_names:
        groups[root(node)].add(node)
    if any(len(group) != 1 for group in groups.values()):
        raise ValueError('wire extraction shorts distinct logical nodes')

    def logical(node):
        names = groups.get(root(node), set())
        if len(names) != 1:
            raise ValueError('unmapped distributed node: ' + node)
        return next(iter(names))

    def devices(text):
        for line in text.splitlines():
            fields = line.split(';')[0].split()
            if fields and fields[0].startswith('X'):
                if len(fields) != 7 or fields[4] not in ('rppd', 'rhigh'):
                    raise ValueError('unsupported resistor array instance')
                parameters = dict(item.split('=') for item in fields[5:])
                if set(parameters) != {'w', 'l'}:
                    raise ValueError('unreviewed resistor parameters')
                yield fields[0], fields[1:4], fields[4], parameters

    def signature(nodes, model, parameters):
        return (model, *sorted(nodes[:2]), nodes[2], number(parameters['w']), number(parameters['l']))

    expected = Counter(signature(nodes, model, params) for _, nodes, model, params in devices(reference_text))
    observed, used, assignments, removal = Counter(), set(), {}, defaultdict(float)
    for name, nodes, model, parameters in devices(distributed_text):
        key = terminals.get((model, *sorted(nodes[:2]), nodes[2]))
        if key is None or key in used:
            raise ValueError('unmatched distributed resistor terminals')
        used.add(key)
        body = bodies[key]
        a, b, bulk = map(logical, nodes)
        if sorted((a, b)) != sorted(body['terminals']) or bulk != body['substrate'] or bulk != substrate:
            raise ValueError('resistor body connected to different logical nets')
        observed[signature([a, b, bulk], model, parameters)] += 1
        for parameter, dimension in (('w', 'width_m'), ('l', 'length_m')):
            if not math.isclose(number(parameters[parameter]), body[dimension], rel_tol=1e-10):
                raise ValueError('distributed resistor geometry changed')
        replacement, geometry = split_body(name + '_body', nodes, body['body'], model,
                                           body['width_m'], body['length_m'])
        assignments[name] = (replacement, dict(body, representation=geometry, identity=list(key)))
        for node in (a, b):
            if node != substrate:
                removal[node] += body['capacitance_F'] / 2
    if expected != observed or used != set(bodies):
        raise ValueError('array extraction changed resistor inventory')

    totals = defaultdict(float)
    for a, b, value in distributed['capacitors']:
        a, b = logical(a), logical(b)
        if a != b and substrate in (a, b):
            totals[b if a == substrate else a] += value
    ratios, rounding = {}, {}
    for node, value in removal.items():
        total = totals[node]
        if total > 0 and 0 < value - total <= 5e-21 + 4 * math.ulp(value):
            rounding[node] = value - total
            removal[node] = value = total
        if total < value or not math.isfinite(total):
            raise ValueError('body capacitance exceeds extracted substrate load: ' + node)
        ratios[node] = (total - value) / total
    output = []
    for line in distributed_text.splitlines():
        fields = line.split(';')[0].split()
        if fields and fields[0] in assignments:
            output.extend(assignments[fields[0]][0].splitlines())
            continue
        if fields and fields[0].startswith('C'):
            a, b = map(logical, fields[1:3])
            if a != b and substrate in (a, b):
                value = number(fields[3], allow_zero=True) * ratios.get(b if a == substrate else a, 1.)
                line = ' '.join(fields[:3]) + f' {value:.17g}'
        output.append(line)
    text = '\n'.join(output) + '\n'
    connections = check_capacitor_connections(text, ports)
    final = inventory(text, ports)
    removed = sum(v for _, _, v in distributed['capacitors']) - sum(v for _, _, v in final['capacitors'])
    if not math.isclose(removed, sum(removal.values()), rel_tol=1e-10, abs_tol=1e-24):
        raise ValueError('body charge partition is not conserved')
    return text, dict(physical_resistors=len(assignments), electrical_segments=2 * len(assignments),
        bodies={name: data for name, (_, data) in assignments.items()}, connections=connections,
        removed_duplicate_body_substrate_F=removed, substrate_load_scale=ratios, output_rounding_F=rounding,
        scope='R3 intrinsic charge replaces Magic body substrate shunts; physical external coupling retained; field/contact calibration pending')
