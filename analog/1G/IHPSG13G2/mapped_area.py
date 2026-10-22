#!/usr/bin/env python3
"""Measure cell area from a functionally checked IHP mapped netlist."""
import argparse
from collections import Counter
import hashlib
import json
import math
from pathlib import Path
import re


def digest(path):
    return hashlib.sha256(Path(path).read_bytes()).hexdigest()


def library_areas(text):
    starts = list(re.finditer(r'\bcell\s*\(\s*"?([^\s"\)]+)"?\s*\)\s*\{', text))
    areas = {}
    for index, match in enumerate(starts):
        body = text[match.end():starts[index + 1].start() if index + 1 < len(starts) else len(text)]
        values = re.findall(r'\barea\s*:\s*([-+\d.eE]+)\s*;', body)
        if len(values) != 1 or match[1] in areas:
            raise ValueError('missing, duplicate or ambiguous cell area: ' + match[1])
        value = float(values[0])
        if not math.isfinite(value) or value <= 0:
            raise ValueError('cell area must be positive: ' + match[1])
        areas[match[1]] = value
    if not areas:
        raise ValueError('Liberty contains no cell areas')
    return areas


def lef_areas(text):
    areas = {}
    for match in re.finditer(r'^MACRO\s+(\S+)\s*\n(.*?)^END\s+\1\s*$', text, re.M | re.S):
        dimensions = re.findall(r'\bSIZE\s+([-+\d.eE]+)\s+BY\s+([-+\d.eE]+)\s*;', match[2])
        if len(dimensions) != 1 or match[1] in areas:
            raise ValueError('missing, duplicate or ambiguous LEF size: ' + match[1])
        width, height = map(float, dimensions[0])
        if not all(math.isfinite(v) and v > 0 for v in (width, height)):
            raise ValueError('LEF dimensions must be positive')
        areas[match[1]] = width * height
    if not areas:
        raise ValueError('LEF contains no macro sizes')
    return areas


def estimate(work, liberty, lef=None):
    work, liberty = Path(work).resolve(), Path(liberty).resolve()
    proof, netlist = work / 'hardware_checks.json', work / 'mapped.v'
    inputs = [proof, netlist, liberty, Path(__file__).resolve()] + ([Path(lef).resolve()] if lef is not None else [])
    input_hashes = {str(path): digest(path) for path in inputs}
    evidence = json.loads(proof.read_text())
    if (evidence.get('status') != 'pass' or evidence.get('native_mapped_exact') is not True
            or evidence.get('mapped_sha256') != digest(netlist)):
        raise ValueError('current exact native/mapped functional evidence is required')
    if not evidence.get('source_sha256') or any(digest(path) != value
            for path, value in evidence['source_sha256'].items()):
        raise ValueError('functional source evidence is absent or stale')
    # Yosys write_verilog -noattr -noexpr emits one flattened cell instance
    # per statement. Count all masters, so an unknown hierarchy cannot vanish
    # silently from the physical area estimate.
    cells = Counter(re.findall(r'^\s{2}(\w+)\s+(?:\\\S+|\w+)\s*\(', netlist.read_text(), re.M))
    expected = evidence.get('cells')
    if not isinstance(expected, dict) or dict(cells) != expected:
        raise ValueError('physical cell inventory differs from checked mapping')
    areas = library_areas(liberty.read_text())
    if set(cells) - set(areas):
        raise ValueError('unknown physical masters: ' + ', '.join(sorted(set(cells) - set(areas))))
    contributions = {name: dict(count=count, cell_area_um2=areas[name], area_um2=count * areas[name])
                     for name, count in sorted(cells.items())}
    total = math.fsum(row['area_um2'] for row in contributions.values())
    result = dict(status='pass', module=evidence.get('module'), cell_count=sum(cells.values()),
        mapped_cell_area_um2=total, mapped_cell_area_mm2=total / 1e6, cells=contributions,
        scope='IHP mapped cell area; excludes physical repair, placement whitespace, routing, fill, analog and padframe',
        input_sha256=input_hashes)
    if lef is not None:
        lef = Path(lef).resolve()
        footprints = lef_areas(lef.read_text())
        if set(cells) - set(footprints):
            raise ValueError('mapped cells are absent from LEF')
        result['mapped_placement_area_um2'] = math.fsum(count * footprints[name] for name, count in cells.items())
        result['liberty_lef_difference_um2'] = result['mapped_placement_area_um2'] - total
        result['liberty_lef_cell_differences_um2'] = {
            name: footprints[name] - areas[name] for name in cells
            if not math.isclose(footprints[name], areas[name], rel_tol=1e-10, abs_tol=1e-10)}
    if any(digest(path) != value for path, value in
           {**input_hashes, **evidence['source_sha256']}.items()):
        raise ValueError('area inputs changed during measurement')
    return result


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--work', type=Path, required=True)
    parser.add_argument('--liberty', type=Path, required=True)
    parser.add_argument('--lef', type=Path, help='also measure placement footprints from actual macro dimensions')
    parser.add_argument('--output', type=Path, required=True)
    args = parser.parse_args()
    args.output.unlink(missing_ok=True)
    result = estimate(args.work, args.liberty, args.lef)
    args.output.parent.mkdir(parents=True, exist_ok=True)
    args.output.write_text(json.dumps(result, indent=2) + '\n')
    print(f'{result["module"]}: {result["cell_count"]} cells, {result["mapped_cell_area_um2"]:.3f} um2')
