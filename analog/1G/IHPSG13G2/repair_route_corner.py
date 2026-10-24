#!/usr/bin/env python3
"""Remove a checked zero end extension at one interior DEF routing bend.

The result still requires new extraction, functional checks, full DRC and LVS.
This does not waive a rule or edit a foundry library.
"""
import argparse
import hashlib
import json
from pathlib import Path
import re


def repair(text, net, x, y):
    begin = re.search(r'(?m)^NETS\s+\d+\s*;', text)
    end = re.search(r'(?m)^END NETS\s*$', text)
    if begin is None or end is None:
        raise ValueError('missing DEF signal-net section')
    nets = list(re.finditer(r'(?m)^\s*-\s+(\S+)', text[begin.end():end.start()]))
    selected = [i for i, match in enumerate(nets) if match[1] == net]
    if len(selected) != 1:
        raise ValueError('requested signal net is absent or duplicated')
    index = selected[0]
    start = begin.end() + nets[index].start()
    stop = begin.end() + nets[index + 1].start() if index + 1 < len(nets) else end.start()
    body = text[start:stop]
    points, current = [], [None, None]
    for match in re.finditer(r'\(\s*(-?\d+|\*)\s+(-?\d+|\*)(?:\s+(-?\d+))?\s*\)', body):
        for axis in (0, 1):
            if match[axis + 1] != '*':
                current[axis] = int(match[axis + 1])
        if None in current:
            raise ValueError('unresolved routing coordinate')
        points.append((match, tuple(current)))
    matches = [i for i, (_, point) in enumerate(points) if point == (x, y)]
    if len(matches) != 1:
        raise ValueError('requested bend is absent or ambiguous')
    index = matches[0]
    if index == 0 or index + 1 == len(points):
        raise ValueError('cannot change a path endpoint')
    previous, point, following = points[index - 1], points[index], points[index + 1]
    match = point[0]
    if match[3] != '0':
        raise ValueError('bend does not have the reviewed zero extension')
    before = body[previous[0].end():match.start()]
    after = body[match.end():following[0].start()]
    if re.search(r'\b(?:NEW|ROUTED)\b|;', before) or after.strip():
        raise ValueError('selected point is a path/via boundary')
    a, b, c = previous[1], point[1], following[1]
    if not ((a[0] == b[0]) != (a[1] == b[1]) and (c[0] == b[0]) != (c[1] == b[1]) and
            (a[0] == b[0]) != (c[0] == b[0])):
        raise ValueError('selected point is not an orthogonal interior bend')
    old = match[0]
    new = '( ' + match[1] + ' ' + match[2] + ' )'
    modified = text[:start + match.start()] + new + text[start + match.end():]
    return modified, dict(net=net, coordinate_dbu=[x, y], before=old, after=new,
        action='use the DEF default half-width end extension at this interior bend')


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--input', type=Path, required=True)
    parser.add_argument('--output', type=Path, required=True)
    parser.add_argument('--net', required=True)
    parser.add_argument('--point', type=int, nargs=2, required=True)
    args = parser.parse_args()
    original = args.input.read_text()
    modified, report = repair(original, args.net, *args.point)
    args.output.parent.mkdir(parents=True, exist_ok=True)
    args.output.write_text(modified)
    report.update(input_sha256=hashlib.sha256(original.encode()).hexdigest(),
        output_sha256=hashlib.sha256(modified.encode()).hexdigest(),
        repair_source_sha256=hashlib.sha256(Path(__file__).read_bytes()).hexdigest(),
        scope='one explicit DEF geometry correction; extraction/DRC/LVS/performance must be rerun')
    args.output.with_suffix('.repair.json').write_text(json.dumps(report, indent=2) + '\n')
