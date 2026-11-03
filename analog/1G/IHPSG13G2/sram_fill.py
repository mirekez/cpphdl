"""Add isolated upper-metal density fill to the SG13G2 frame SRAM candidate.

Only filler datatypes on Metal5 and TopMetal1/2 change. This is geometry
generation; complete DRC, LVS and post-fill electrical checks remain required.
"""
import argparse
import hashlib
import json
from pathlib import Path

import klayout.db as db

from sram_reference import MACRO

# PDK filler sizes and keepouts; the Metal5 lattice uses a 0.5 um gap,
# above the 0.42 um minimum. Top-metal spacing matches the PDK macro.
PATTERNS = ((67, 1.0, 1.0, .5, .42, 1.0, 35, 60),
            (126, 5.0, 10.0, 3.05, 3.0, 4.9, 25, 70),
            (134, 5.0, 10.0, 3.05, 3.0, 4.9, 25, 70))


def add_fill(layout, top):
    if layout.dbu != .001 or top.bbox().empty():
        raise ValueError('fill requires nonempty geometry on a 1 nm grid')
    extent = top.bbox()

    def layer(number, datatype):
        return db.Region(top.begin_shapes_rec(layout.layer(number, datatype)))

    # Reject unexpected existing upper-metal shapes before modifying anything.
    for number, *_ in PATTERNS:
        if not (layer(number, 0) | layer(number, 22)).is_empty():
            raise ValueError('expected unused SRAM upper-metal layer: ' + str(number))
        if layout.cell(f'SRAM_FILL_{number}') is not None:
            raise ValueError('fill cell name already exists')
    result = {}
    for number, width, height, gap, clearance, transistor_gap, minimum, maximum in PATTERNS:
        def units(value):
            return round(value / layout.dbu)
        blocked = (layer(26, 0).sized(units(transistor_gap)) |
                   layer(number, 23) | layer(number, 24) | layer(160, 0))
        # Keep every fill tile inside the original macro boundary, with space
        # for neighboring macros. No via or signal-drawing shape is added.
        available = db.Region(extent).sized(-units(clearance)) - blocked
        tile = layout.create_cell(f'SRAM_FILL_{number}')
        box = db.Box(0, 0, units(width), units(height))
        tile.shapes(layout.layer(number, 22)).insert(box)
        top.fill_region(available, tile.cell_index(), box,
                        db.Vector(units(width + gap), 0), db.Vector(0, units(height + gap)),
                        db.Point(extent.left, extent.bottom))
        fill = layer(number, 22)
        if not (fill - available).is_empty():
            raise ValueError('fill crossed its exclusion region')
        density = 100 * fill.merged().area() / extent.area()
        if not minimum <= density <= maximum:
            raise ValueError(f'layer {number} fill density outside limits: {density}')
        result[str(number)] = dict(density_percent=density, tiles=fill.count(),
                                   gap_um=gap, minimum_percent=minimum, maximum_percent=maximum)
    if top.bbox() != extent:
        raise ValueError('fill changed macro boundary')
    return result


def build(source, output, report):
    source, output, report = map(Path, (source, output, report))
    if source.resolve() in (output.resolve(), report.resolve()) or output.resolve() == report.resolve():
        raise ValueError('input, GDS output and report must be distinct')
    output.unlink(missing_ok=True)
    report.unlink(missing_ok=True)
    digest = hashlib.sha256(source.read_bytes()).hexdigest()
    layout = db.Layout()
    layout.read(str(source))
    if len(layout.top_cells()) != 1 or layout.top_cell().name != MACRO:
        raise ValueError('expected the complete frame SRAM macro')
    result = add_fill(layout, layout.top_cell())
    if hashlib.sha256(source.read_bytes()).hexdigest() != digest:
        raise ValueError('input changed during fill generation')
    layout.write(str(output))
    evidence = dict(status='candidate_generated', source_sha256=digest,
                    output_sha256=hashlib.sha256(output.read_bytes()).hexdigest(),
                    modified_layers=[[n, 22] for n, *_ in PATTERNS], layers=result,
                    full_sram_qualified=False, full_chip_qualified=False)
    report.write_text(json.dumps(evidence, indent=2) + '\n')
    return evidence


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    for name in ('source', 'output', 'report'):
        parser.add_argument('--' + name, type=Path, required=True)
    args = parser.parse_args()
    build(args.source, args.output, args.report)
