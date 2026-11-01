"""Conserve the extracted capacitance matrix across Magic resistance splitting.

Magic issue #550 adds each coupling capacitor to both substrate loads while
also retaining that coupling. Correct only this measured, identifiable case.
Resistors and device geometry are unchanged. Coupling remains lumped at its
original net attachment; substrate capacitance uses Magic's area distribution.
"""
from collections import Counter, defaultdict
import math


def conserve(reference, distributed, text, substrate):
    parent = {}

    def root(node):
        parent.setdefault(node, node)
        if parent[node] != node:
            parent[node] = root(parent[node])
        return parent[node]

    for a, b, _ in distributed['resistors']:
        parent[root(a)] = root(b)
    original = set(n for row in reference['capacitors'] for n in row[:2])
    aliases = defaultdict(set)
    for node in original:
        aliases[root(node)].add(node)
    if any(len(names) != 1 for names in aliases.values()):
        raise ValueError('resistance extraction shorts distinct original nets')
    names = {group: next(iter(values)) for group, values in aliases.items()}
    if substrate not in original:
        raise ValueError('missing explicit reference substrate')

    def logical(node):
        if root(node) not in names:
            raise ValueError('unmapped extracted capacitor endpoint: ' + node)
        return names[root(node)]

    def matrix(records):
        result = defaultdict(float)
        for a, b, value in records:
            a, b = logical(a), logical(b)
            if a == b:
                # Finite ground wiring can have capacitance to the substrate
                # at another point on that same logical net. Its two ends are
                # equipotential only in this collapsed comparison. Keep the
                # actual distributed element; its collapsed energy is zero.
                continue
            result[tuple(sorted((a, b)))] += value
        return result

    wanted, observed = matrix(reference['capacitors']), matrix(distributed['capacitors'])
    ratios, audit = {}, {}
    for a, b in set(wanted) | set(observed):
        expected, actual = wanted[a, b], observed[a, b]
        if substrate not in (a, b):
            if not math.isclose(expected, actual, rel_tol=5e-5, abs_tol=1e-20):
                raise ValueError('resistance extraction lost or changed coupling')
            continue
        node = b if a == substrate else a
        coupling = sum(value for pair, value in wanted.items() if node in pair and substrate not in pair)
        if math.isclose(actual, expected, rel_tol=5e-5, abs_tol=1e-20):
            ratio, reason = 1., 'already conserved'
        # Magic distributes with single-precision area accumulators and emits
        # rounded decimal node loads. Permit 0.01% only when diagnosing this
        # duplication; the corrected matrix must satisfy the tighter check.
        elif expected > 0 and math.isclose(actual, expected + coupling, rel_tol=1e-4, abs_tol=1e-20):
            ratio, reason = expected / actual, 'Magic #550 coupling duplicated onto substrate'
        else:
            raise ValueError('unexplained substrate capacitance change: ' + node)
        ratios[node] = ratio
        audit[node] = dict(reference_F=expected, raw_F=actual, coupling_F=coupling,
                           substrate_distribution_scale=ratio, reason=reason)
    output, corrected, counter, internal = [], [], Counter(), []
    for line in text.splitlines():
        fields = line.split(';')[0].split()
        if fields and fields[0].startswith('C'):
            a, b = logical(fields[1]), logical(fields[2])
            pair = tuple(sorted((fields[1], fields[2])))
            # Use the parsed original value, not a second SPICE number parser.
            index = counter['capacitor']
            old_a, old_b, value = distributed['capacitors'][index]
            if pair != tuple(sorted((old_a, old_b))):
                raise ValueError('changed raw capacitance ordering')
            counter['capacitor'] += 1
            if a == b:
                internal.append(dict(a=fields[1], b=fields[2], capacitance_F=value, logical_net=a))
            elif substrate in (a, b):
                value *= ratios[b if a == substrate else a]
                line = ' '.join(fields[:3]) + f' {value:.17g}'
            corrected.append((fields[1], fields[2], value))
        output.append(line)
    final = matrix(corrected)
    if counter['capacitor'] != len(distributed['capacitors']) or any(
            not math.isclose(wanted[pair], final[pair], rel_tol=5e-5, abs_tol=1e-20)
            for pair in set(wanted) | set(final)):
        raise ValueError('corrected capacitance matrix does not conserve the reference')
    return '\n'.join(output) + '\n', dict(substrate=substrate, nodes=audit,
        retained_intranet_capacitors=internal,
        corrected_nodes=sum(row['substrate_distribution_scale'] != 1 for row in audit.values()),
        reference_total_F=sum(wanted.values()), corrected_total_F=sum(final.values()),
        scope='distributed resistance, area-distributed substrate capacitance and lumped coupling; no field-solver validation')
