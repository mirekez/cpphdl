#!/usr/bin/env python3
"""Render the complete published GDS hierarchy at selectable pixel resolution."""
import argparse
from datetime import datetime, timezone
import json
import math
from pathlib import Path
import tempfile

from PIL import Image, ImageDraw

from package_development_layout import ROOT, digest, read_block
from package_receive_results import BACKGROUND, ACCENT, MUTED, LAYERS, fitted_box, label, render


def run(args):
    if not 2000 <= args.width <= 16000:
        raise ValueError('width must be between 2000 and 16000 pixels')
    provenance = args.provenance.resolve()
    report = json.loads(provenance.read_text())
    gds = provenance.parent / 'repeater_development.gds.gz'
    gds_hash, provenance_hash = digest(gds), digest(provenance)
    if report['artifacts'][gds.name]['sha256'] != gds_hash:
        raise ValueError('GDS differs from publication provenance')
    layout, top = read_block(gds, report['top'])
    instances = list(top.each_inst())
    blocks = report['blocks']
    floorplan = report.get('floorplan')
    if len(list(layout.top_cells())) != 1 or sorted(i.cell.name for i in instances) != sorted(
            b['assembly_cell'] for b in blocks):
        raise ValueError('merged hierarchy differs from block inventory')
    box = top.dbbox()
    bounds = [box.left, box.bottom, box.right, box.top]
    if any(abs(a - b) > layout.dbu for a, b in zip(bounds, report['bounds_um'])):
        raise ValueError('merged bounds differ from publication provenance')
    del instances, top, layout

    width = args.width
    unit = width / 8000
    margin = round(100 * unit)
    tile_width = width - 2 * margin
    tile_height = math.ceil(tile_width * (bounds[3] - bounds[1]) / (bounds[2] - bounds[0]))
    header = round(250 * unit)
    legend_rows = math.ceil(len(blocks) / 3)
    height = header + tile_height + round((260 + 68 * legend_rows) * unit)
    args.output.parent.mkdir(parents=True, exist_ok=True)
    args.work.mkdir(parents=True, exist_ok=True)
    with tempfile.TemporaryDirectory(prefix='full-layout-', dir=args.work) as temporary:
        tile, ratio = render(gds, Path(temporary), 'geometry', list(LAYERS), bounds,
                             tile_width, tile_height)
        canvas = Image.new('RGB', (width, height), BACKGROUND)
        canvas.paste(tile, (margin, header))
        del tile
        draw = ImageDraw.Draw(canvas)
        label(draw, (margin, round(35 * unit)), '1G repeater / ' + ('single-die floorplan' if floorplan else 'complete current GDS assembly'), round(68 * unit))
        label(draw, (margin, round(125 * unit)),
              f'{len(blocks)} physical blocks | IHP SG13G2 | inter-block connections unfinished',
              round(42 * unit), ACCENT)
        view = fitted_box(bounds, tile_width, tile_height)
        for index, block in enumerate(blocks):
            x0, y0, x1, y1 = block['bounds_um']
            x = margin + round((x0 - view.left) * ratio)
            y = header + round((view.top - y1) * ratio)
            tag = f'{index + 1:02d}'
            draw.rectangle((x, y, x + round(76 * unit), y + round(60 * unit)), fill=BACKGROUND)
            label(draw, (x + round(6 * unit), y), tag, round(42 * unit), ACCENT)
            column, row = index % 3, index // 3
            label(draw, (margin + column * tile_width // 3,
                         header + tile_height + round((55 + row * 68) * unit)),
                  f'{tag}  {block["title"]}', round(38 * unit), MUTED)
        label(draw, (margin, height - round(100 * unit)),
              f'All drawing layers | {bounds[2] - bounds[0]:.2f} x {bounds[3] - bounds[1]:.2f} um ' + ('proposed die' if floorplan else 'review arrangement'),
              round(36 * unit), MUTED)
        temporary_png = Path(temporary) / args.output.name
        canvas.save(temporary_png, optimize=True)
        with Image.open(temporary_png) as check:
            check.verify()
        if digest(gds) != gds_hash or digest(provenance) != provenance_hash:
            raise ValueError('publication changed during rendering')
        temporary_png.replace(args.output)
    receipt = dict(generated_utc=datetime.now(timezone.utc).isoformat(),
                   gds=str(gds), gds_sha256=gds_hash, provenance_sha256=provenance_hash,
                   renderer_sha256=digest(Path(__file__)),
                   drawing_layers=LAYERS, blocks=len(blocks), top=report['top'],
                   width_px=width, height_px=height, full_chip_qualified=False,
                   electrically_integrated=report['electrically_integrated'],
                   png_sha256=digest(args.output))
    args.output.with_suffix('.json').write_text(json.dumps(receipt, indent=2) + '\n')
    print(json.dumps(dict(output=str(args.output), width=width, height=height, blocks=len(blocks))), flush=True)


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--provenance', type=Path, default=ROOT / '1G/IHPSG13G2/results/development_provenance.json')
    parser.add_argument('--output', type=Path, default=ROOT / '1G/IHPSG13G2/results/repeater_full_resolution.png')
    parser.add_argument('--width', type=int, default=8000)
    parser.add_argument('--work', type=Path, default=ROOT / 'build/layout-publication')
    run(parser.parse_args())
