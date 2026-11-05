#!/usr/bin/env python3
"""Publish only a DRC/LVS checked physical clock/controller subsystem."""
import argparse
import gzip
import json
from pathlib import Path
import tempfile

from PIL import Image, ImageDraw

from integrate_clock_control import ROOT, TOP
from package_development_layout import digest, read_block
from package_receive_results import BACKGROUND, ACCENT, MUTED, LAYERS, label, render


def run(args):
    proof = json.loads(args.checks.read_text())
    if (proof.get('status') != 'physical_checks_pass' or not proof.get('drc_qualified') or
            not proof.get('lvs_qualified') or not proof.get('opened_routing_rejected') or
            proof.get('transistor_lvs', {}).get('lvs', {}).get('instantiated_devices') != 20938):
        raise ValueError('complete clock/control physical checks required')
    hashes = {**proof['source_sha256'], **proof['artifact_sha256'], str(args.checks.resolve()): digest(args.checks)}
    def verify():
        for name, expected in hashes.items():
            if digest(Path(name)) != expected:
                raise ValueError('changed physical proof: ' + name)
    verify()
    gds = Path(proof['gds'])
    layout, top = read_block(gds, TOP)
    box = top.dbbox()
    bounds = [box.left, box.bottom, box.right, box.top]
    layout.destroy()
    args.output.mkdir(parents=True, exist_ok=True)
    with tempfile.TemporaryDirectory(prefix='clock-control-publication-') as temporary:
        work = Path(temporary)
        canvas = Image.new('RGB', (3000, 3500), BACKGROUND)
        draw = ImageDraw.Draw(canvas)
        label(draw, (60, 25), 'Processing clock / physically connected controller and oscillator', 44)
        label(draw, (60, 95), '21 inter-macro nets | 20,938 devices | full DRC and transistor LVS passed', 34, ACCENT)
        tile, _ = render(gds, work, 'all', list(LAYERS), bounds, 1800, 3000)
        canvas.paste(tile, (60, 200))
        label(draw, (1920, 200), 'Upper routing layers', 35)
        tile, _ = render(gds, work, 'routing', list(LAYERS)[7:], bounds, 1020, 1300)
        canvas.paste(tile, (1920, 270))
        terminals = proof['connections']['positive']['terminals']
        points = [terminals[name]['point_dbu'] for name in
                  ('ClockDeliveryMacro/RAW', 'ClockControlMacro/pcs_clk')]
        detail = [min(p[0] for p in points) / 1000 - 30,
                  min(p[1] for p in points) / 1000 - 20,
                  max(p[0] for p in points) / 1000 + 30,
                  max(p[1] for p in points) / 1000 + 20]
        label(draw, (1920, 1640), 'RAW terminals / actual metal', 33)
        tile, _ = render(gds, work, 'raw-detail', list(LAYERS)[7:], detail, 1020, 1450)
        canvas.paste(tile, (1920, 1710))
        label(draw, (60, 3260), f'{box.width():.2f} x {box.height():.2f} um | cut inter-block wires fail extraction', 36, ACCENT)
        label(draw, (60, 3340), 'Connected-load electrical and timing qualification remain open.', 35, MUTED)
        label(draw, (60, 3410), 'This is one connected subsystem; the full repeater is not integrated.', 35, MUTED)
        png = work / 'processing_clock_system.png'
        canvas.save(png, optimize=True)
        packed = work / 'processing_clock_system.gds.gz'
        packed.write_bytes(gzip.compress(gds.read_bytes(), mtime=0))
        receipt = dict(top=TOP, source_checks=str(args.checks.resolve()),
            source_checks_sha256=digest(args.checks), width_um=box.width(), height_um=box.height(),
            inter_macro_connections=21, lvs=proof['transistor_lvs']['lvs'], drc=proof['drc'],
            opened_routing_rejected=True, drc_qualified=True, lvs_qualified=True,
            electrical_qualified=False, full_chip_qualified=False,
            artifacts={p.name: digest(p) for p in (packed, png)})
        verify()
        for path in (packed, png):
            path.replace(args.output / path.name)
        (args.output / 'processing_clock_system.json').write_text(json.dumps(receipt, indent=2) + '\n')
    print(str(args.output / 'processing_clock_system.png'))


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--checks', type=Path, required=True)
    parser.add_argument('--output', type=Path, default=ROOT / '1G/IHPSG13G2/results')
    run(parser.parse_args())
