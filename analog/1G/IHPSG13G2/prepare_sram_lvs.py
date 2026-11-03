#!/usr/bin/env python3
"""Prepare a reproducible SRAM extraction fixture without changing device geometry."""
import argparse
import hashlib
import json
from pathlib import Path
import re
import shutil

import klayout.db as db

from sram_fill import add_fill
from sram_reference import MACRO, build as build_reference, reachable

HERE = Path(__file__).resolve().parent


def digest(path):
    with Path(path).open('rb') as stream:
        return hashlib.file_digest(stream, 'sha256').hexdigest()


def prepare(pdk, output, filled=False):
    pdk, output = pdk.resolve(), output.resolve()
    output.mkdir(parents=True, exist_ok=False)
    reference = output / 'reference.cir'
    reference_report = build_reference(pdk, reference, output / 'reference.json')
    circuits, _ = reachable(reference.read_text(), MACRO)
    source = pdk / 'ihp-sg13g2/libs.ref/sg13g2_sram/gds' / (MACRO + '.gds')
    original = pdk / 'ihp-sg13g2/libs.tech/klayout/tech/lvs'
    inputs = [source, Path(__file__).resolve(), HERE / 'sram_lvs_alignment.rb', HERE / 'sram_lvs_hints.rb',
              HERE / 'sram_reference.py', HERE / 'sram_fill.py', *[p for p in original.rglob('*') if p.is_file()]]
    versions = pdk / 'versions.txt'
    if versions.is_file():
        inputs.append(versions)
    input_hashes = dict(reference_report['source_sha256'])
    for path in inputs:
        value = digest(path)
        if str(path) in input_hashes and input_hashes[str(path)] != value:
            raise ValueError('reference generation input changed')
        input_hashes[str(path)] = value
    layout = db.Layout()
    layout.read(str(source))
    top = layout.top_cell()
    boundary = top.bbox()
    alignment = (HERE / 'sram_lvs_alignment.rb').read_text()
    match = re.search(r'^  keep_circuits = (\[.*\])$', alignment, re.M)
    if match is None:
        raise ValueError('missing explicit retained hierarchy')
    keep = set(json.loads(match[1]))
    names = {}
    for cell in layout.each_cell():
        if cell == top or not cell.name.startswith('RM_IHPSG13_2P_'):
            continue
        target = cell.name.upper().replace('RM_IHPSG13_2P_', 'RM_IHPSG13_1024X16_C2_2P_', 1)
        if target not in circuits:
            # Geometry-only helper cells have no schematic boundary. They
            # remain in the layout and are expanded during extraction.
            continue
        names[cell.name] = target
    if len(names) != 20:
        raise ValueError('unexpected physical hierarchy inventory')
    for old, new in names.items():
        if layout.cell(new) is not None:
            raise ValueError('hierarchy rename collision')
        layout.cell(old).name = new
    removed = 0
    for cell in layout.each_cell():
        if cell.name.upper() in keep:
            continue
        for layer in layout.layer_indexes():
            for shape in [s for s in cell.shapes(layer).each() if s.is_text()]:
                shape.delete()
                removed += 1
    fill = add_fill(layout, top) if filled else None
    if top.bbox() != boundary:
        raise ValueError('preparation changed the macro boundary')
    gds = output / 'macro.gds'
    layout.write(str(gds))
    deck = output / 'pdk/ihp-sg13g2/libs.tech/klayout/tech/lvs'
    shutil.copytree(original, deck)
    if versions.is_file():
        shutil.copyfile(versions, output / 'pdk/versions.txt')
    text = (original / 'sg13g2.lvs').read_text()
    align_anchor = "  logger.info('Starting SG13G2 LVS Alignment')\n"
    compare_anchor = '  # === COMPARISON ===\n'
    if text.count(align_anchor) != 1 or text.count(compare_anchor) != 1:
        raise ValueError('PDK comparison insertion points changed')
    text = text.replace(align_anchor, alignment + align_anchor)
    hints = (HERE / 'sram_lvs_hints.rb').read_text()
    text = text.replace(compare_anchor, compare_anchor + '  max_depth(2)\n  max_branch_complexity(20)\n' + hints)
    (deck / 'sg13g2.lvs').write_text(text)
    if any(digest(Path(name)) != value for name, value in input_hashes.items()):
        raise ValueError('preparation input changed')
    report = dict(status='prepared', name_correspondences=names, internal_texts_removed=removed,
                  fill=fill, source_sha256=input_hashes,
                  artifact_sha256={str(p): digest(p) for p in
                                   (gds, reference, output / 'reference.json', deck / 'sg13g2.lvs')},
                  full_sram_qualified=False, full_chip_qualified=False)
    (output / 'preparation.json').write_text(json.dumps(report, indent=2) + '\n')
    return report


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--pdk', type=Path, required=True)
    parser.add_argument('--output', type=Path, required=True)
    parser.add_argument('--fill', action='store_true')
    args = parser.parse_args()
    prepare(args.pdk, args.output, args.fill)
