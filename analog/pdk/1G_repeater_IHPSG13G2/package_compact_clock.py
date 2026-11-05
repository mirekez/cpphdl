#!/usr/bin/env python3
"""Publish the checked compact clock and render its actual device/routing layers."""
import argparse
import gzip
import json
from pathlib import Path
import tempfile

from PIL import Image, ImageDraw

from package_development_layout import ROOT, digest, read_block
from package_receive_results import ACCENT, BACKGROUND, MUTED, LAYERS, label, render


def run(args):
    proof = json.loads(args.checks.read_text())
    if (proof.get('status') != 'pass' or not proof.get('lvs_qualified') or
            not proof.get('drc_qualified') or not proof.get('opened_clock_trunk_rejected') or
            proof.get('lvs', {}).get('instantiated_devices') != 398):
        raise ValueError('publication requires full compact-clock geometry verification')
    hashes = {**proof['source_sha256'], **proof['artifact_sha256'], str(args.checks): digest(args.checks)}
    for name, expected in hashes.items():
        if digest(Path(name)) != expected:
            raise ValueError('changed verification input: ' + name)
    gds = Path(proof['gds'])
    if str(gds) not in proof['artifact_sha256']:
        raise ValueError('verified GDS missing from artifact receipt')
    comparison = None
    if args.comparison:
        comparison = json.loads(args.comparison.read_text())
        if (comparison.get('status') != 'pin_load_screen_pass' or
                comparison.get('compact_gds_sha256') != digest(gds)):
            raise ValueError('pin-load comparison failed or refers to different geometry')
        hashes[str(args.comparison)] = digest(args.comparison)
        hashes.update(comparison['source_sha256'])
    layout, top = read_block(gds, 'ProcessingClockDelivery')
    box = top.dbbox()
    bounds = [box.left, box.bottom, box.right, box.top]
    del top, layout
    args.output.mkdir(parents=True, exist_ok=True)
    with tempfile.TemporaryDirectory(prefix='compact-clock-') as temporary:
        work = Path(temporary)
        canvas = Image.new('RGB', (3600, 3100), BACKGROUND)
        draw = ImageDraw.Draw(canvas)
        label(draw, (70, 30), 'Connected processing clock / compact device placement', 57)
        label(draw, (70, 110), f'{box.width():.2f} x {box.height():.2f} um | 398 devices | '
              f'{proof["area_reduction_percent"]:.2f}% less bounding area', 39, ACCENT)
        tile, _ = render(gds, work, 'complete', list(LAYERS), bounds, 3460, 2200)
        canvas.paste(tile, (70, 190))
        for i, (title, layers) in enumerate((('Devices and lower routing', list(LAYERS)[:9]),
                                            ('Shared clock and supply routing', list(LAYERS)[9:]))):
            x = 70 + 1750 * i
            label(draw, (x, 2410), title, 34)
            tile, _ = render(gds, work, 'detail-' + str(i), layers, bounds, 1700, 480)
            canvas.paste(tile, (x, 2465))
        label(draw, (70, 2975), 'Full DRC and transistor LVS passed; extracted electrical qualification pending.', 35, MUTED)
        label(draw, (70, 3030), 'Oscillator + admission gate. Controller, receiver loads and full chip remain separate.', 35, MUTED)
        png = work / 'processing_clock_delivery.png'
        canvas.save(png, optimize=True)
        compressed = work / 'processing_clock_delivery.gds.gz'
        compressed.write_bytes(gzip.compress(gds.read_bytes(), mtime=0))
        for name, expected in hashes.items():
            if digest(Path(name)) != expected:
                raise ValueError('verification changed during rendering')
        receipt = dict(top='ProcessingClockDelivery', source_checks=str(args.checks.resolve()),
            source_checks_sha256=digest(args.checks), gds_sha256=digest(gds),
            width_um=box.width(), height_um=box.height(),
            area_reduction_percent=proof['area_reduction_percent'], lvs=proof['lvs'], drc=proof['drc'],
            full_chip_qualified=False, electrical_qualified=False,
            renderer_sha256=digest(Path(__file__)),
            parasitic_comparison=comparison,
            artifacts={p.name: digest(p) for p in (png, compressed)})
        for path in (png, compressed):
            path.replace(args.output / path.name)
        (args.output / 'processing_clock_delivery.json').write_text(json.dumps(receipt, indent=2) + '\n')
    print(str(args.output / 'processing_clock_delivery.png'))


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--checks', type=Path, required=True)
    parser.add_argument('--comparison', type=Path)
    parser.add_argument('--output', type=Path, default=ROOT / '1G/IHPSG13G2/results')
    run(parser.parse_args())
