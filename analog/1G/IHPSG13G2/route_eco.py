"""Preserve untouched routing guides while applying a checked local repair."""
from collections import OrderedDict
import re


def read_guides(text):
    lines = [line.strip() for line in text.splitlines() if line.strip()]
    result = OrderedDict()
    index = 0
    while index < len(lines):
        name = lines[index]
        if name in result or name in ('(', ')') or len(name.split()) != 1:
            raise ValueError('duplicate or invalid routing-guide net')
        index += 1
        if index >= len(lines) or lines[index] != '(':
            raise ValueError('missing routing-guide opening delimiter')
        index += 1
        rectangles = []
        while index < len(lines) and lines[index] != ')':
            fields = lines[index].split()
            if len(fields) != 5 or re.fullmatch(r'[A-Za-z_]\w*', fields[4]) is None:
                raise ValueError('invalid routing-guide rectangle or layer')
            x0, y0, x1, y1 = map(int, fields[:4])
            if x0 >= x1 or y0 >= y1:
                raise ValueError('empty or reversed routing-guide rectangle')
            rectangles.append((x0, y0, x1, y1, fields[4]))
            index += 1
        if index >= len(lines) or not rectangles:
            raise ValueError('incomplete routing-guide net')
        index += 1
        result[name] = rectangles
    if not result:
        raise ValueError('empty routing-guide file')
    return result


def merge_guides(original, updated, affected):
    baseline, delta = read_guides(original), read_guides(updated)
    affected = set(affected)
    if not affected or not affected <= delta.keys():
        raise ValueError('local repair is missing affected-net guides')
    for name in delta.keys() - affected:
        if name not in baseline or sorted(delta[name]) != sorted(baseline[name]):
            raise ValueError('local repair changed an unselected net guide')
    for name in affected:
        baseline[name] = delta[name]
    return '\n'.join(name + '\n(\n' + '\n'.join(' '.join(map(str, rectangle)) for rectangle in rectangles) + '\n)'
                     for name, rectangles in baseline.items()) + '\n'
