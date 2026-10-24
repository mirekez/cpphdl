"""Save all checked node voltages, including both sides of a difference."""
import re


def keep_observed_differential_voltages(deck):
    if deck.count('.control\n') != 1 or re.search(r'(?im)^\s*\.save\b', deck):
        raise ValueError('expected one control block and no existing save selection')
    nodes, indices = [], []
    for line in (line for line in deck.splitlines() if line.startswith('let observation')):
        match = re.fullmatch(r'let observation(\d+) = v\((n\d+)\)(?: - v\((n\d+)\))?', line)
        if not match:
            raise ValueError('save selection supports node voltages and their explicit differences only')
        indices.append(int(match[1]))
        for node in match.groups()[1:]:
            if node is not None and node not in nodes:
                nodes.append(node)
    if not nodes or indices != list(range(len(indices))):
        raise ValueError('missing or reordered voltage observations')
    selection = '.save ' + ' '.join('v(' + node + ')' for node in nodes) + '\n'
    return deck.replace('.control\n', selection + '.control\n', 1)
