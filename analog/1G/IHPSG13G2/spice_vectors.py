"""Retain checked voltages without recording every internal PSP model vector."""
import re


def keep_observed_voltages(deck):
    if deck.count('.control\n') != 1 or re.search(r'(?im)^\s*\.save\b', deck):
        raise ValueError('expected one control block and no existing save selection')
    expressions = [line for line in deck.splitlines() if line.startswith('let observation')]
    nodes, indices = [], []
    for line in expressions:
        match = re.fullmatch(r'let observation(\d+) = v\((n\d+)\)', line)
        if not match:
            raise ValueError('save selection supports explicit voltage observations only')
        indices.append(int(match[1]))
        if match[2] not in nodes:
            nodes.append(match[2])
    if not nodes or indices != list(range(len(indices))):
        raise ValueError('missing or reordered voltage observations')
    selection = '.save ' + ' '.join('v(' + node + ')' for node in nodes) + '\n'
    return deck.replace('.control\n', selection + '.control\n', 1)
