"""Package a verified receiver-reference GDS and render its actual mask geometry.

Use the PDK Python with Pillow available (for the local installation,
PYTHONPATH=out/pdf-renderer). This reads completed geometry evidence only;
it does not start simulation or claim electrical/full-chip qualification.
"""
import argparse
from datetime import datetime, timezone
import gzip
import hashlib
import json
from pathlib import Path
import shutil
import subprocess
import tempfile
import xml.etree.ElementTree as ET

import klayout.db as db
import klayout.lay as lay
from PIL import Image, ImageDraw, ImageFont

ROOT = Path(__file__).resolve().parents[2]
BACKGROUND, INK, MUTED, ACCENT = '#101923', '#eff5fc', '#acc0d1', '#73e2ce'
FONT = '/usr/share/fonts/truetype/dejavu/DejaVuSans.ttf'
LAYERS = {
    'Active': (1, '#60db7c'), 'Poly': (5, '#e77773'), 'Contact': (6, '#d6f3fa'),
    'Metal1': (8, '#54b9f2'), 'Via1': (19, '#e6effb'), 'Metal2': (10, '#d4cedf'),
    'Via2': (29, '#efbaa0'), 'Metal3': (30, '#f57e87'), 'Via3': (49, '#c5d49a'),
    'Metal4': (50, '#9bdd63'), 'Via4': (66, '#e7b57a'), 'Metal5': (67, '#e9ce5b'),
    'TopVia1': (125, '#ffe3be'), 'TopMetal1': (126, '#ffe3be'),
    'TopVia2': (133, '#f8ad61'), 'TopMetal2': (134, '#f8ad61'),
}


def digest(path):
    with Path(path).open('rb') as stream:
        return hashlib.file_digest(stream, 'sha256').hexdigest()


def verify(hashes):
    if not hashes:
        raise ValueError('missing geometry provenance')
    for path, expected in hashes.items():
        if not Path(path).is_file() or digest(path) != expected:
            raise ValueError('changed geometry input or artifact: ' + path)


def label(draw, xy, value, size=28, color=INK):
    draw.text(xy, value, font=ImageFont.truetype(FONT, size), fill=color)


def fitted_box(bounds, width, height):
    x0, y0, x1, y1 = bounds
    cx, cy = (x0 + x1) / 2, (y0 + y1) / 2
    span = max(x1 - x0, (y1 - y0) * width / height)
    return db.DBox(cx - span / 2, cy - span * height / width / 2,
                   cx + span / 2, cy + span * height / width / 2)


def render(gds, work, name, layers, bounds, width, height):
    properties = ET.Element('layer-properties')
    for key in layers:
        number, color = LAYERS[key]
        prop = ET.SubElement(properties, 'properties')
        for key, value in dict(name=key, source=f'{number}/0', **{
                'frame-color': color, 'fill-color': color, 'visible': 'true',
                'valid': 'true', 'dither-pattern': 'I0'}).items():
            ET.SubElement(prop, key).text = value
    lyp = work / (name + '.lyp')
    ET.ElementTree(properties).write(lyp)
    view = lay.LayoutView()
    try:
        view.set_config('background-color', BACKGROUND)
        view.set_config('grid-visible', 'false')
        view.set_config('text-visible', 'false')
        view.load_layout(str(gds), False)
        view.load_layer_props(str(lyp), 0, False)
        view.max_hier()
        box = fitted_box(bounds, width, height)
        png = work / (name + '.png')
        view.save_image_with_options(str(png), width, height, 1, 1, 1, box, False)
        with Image.open(png) as image:
            return image.convert('RGB'), width / box.width()
    finally:
        view.destroy()


def scale(draw, x, y, pixels_per_um, length):
    end = x + round(pixels_per_um * length)
    draw.line((x, y, end, y), fill=INK, width=4)
    draw.line((x, y - 6, x, y + 6), fill=INK, width=3)
    draw.line((end, y - 6, end, y + 6), fill=INK, width=3)
    label(draw, (end + 15, y - 15), f'{length} µm', 23, MUTED)


def pictures(gds, work, destination, geometry, bounds):
    canvas = Image.new('RGB', (2400, 1600), BACKGROUND)
    draw = ImageDraw.Draw(canvas)
    label(draw, (70, 35), '1G receiver reference / storage-access revision', 42)
    label(draw, (70, 96), f'IHP SG13G2  ·  {geometry["width_um"]:.2f} × {geometry["height_um"]:.2f} µm'
          f'  ·  {geometry["bounding_area_um2"] / 1e6:.4f} mm²', 27, ACCENT)
    groups = [('Complete drawing-layer view', list(LAYERS)),
              ('Devices and lower routing', list(LAYERS)[:9]),
              ('Upper routing and storage access', list(LAYERS)[9:])]
    for index, (title, selected) in enumerate(groups):
        y = 170 + index * 450
        label(draw, (70, y), title, 29)
        image, ratio = render(gds, work, f'overview-{index}', selected, bounds, 2260, 360)
        canvas.paste(image, (70, y + 42))
        scale(draw, 100, y + 413, ratio, 200)
    label(draw, (70, 1535), 'Local geometry + LVS checked. Unfilled standalone macro; electrical and full-chip qualification remain open.', 24, MUTED)
    canvas.save(destination / 'receive_reference_layers.png', optimize=True)

    canvas = Image.new('RGB', (2400, 1550), BACKGROUND)
    draw = ImageDraw.Draw(canvas)
    label(draw, (70, 35), 'Receiver reference / physical storage connections', 42)
    label(draw, (70, 98), 'Actual GDSII masks  ·  all original devices and intentional capacitors retained', 27, ACCENT)
    components = {p['name']: p for p in geometry['components']}
    panels = [('ReceiveCommonStorage17', 'Divider storage / ground return', 50),
              ('ReceiveCommonStorage9', 'Output storage / direct plate access', 100)]
    for index, (name, title, microns) in enumerate(panels):
        part = components[name]
        x, y = part['origin_um']
        g = part['geometry']
        bounds = (x - 65, geometry['terminals']['VSS']['y_um'] - 20,
                  x + g['width_um'] + 30, y + g['height_um'] + 35)
        px = 70 + index * 1190
        label(draw, (px, 175), title, 29)
        image, ratio = render(gds, work, f'storage-{index}', list(LAYERS)[9:], bounds, 1120, 1050)
        canvas.paste(image, (px, 225))
        scale(draw, px + 20, 1300, ratio, microns)
        label(draw, (px, 1340), f'{g["capacitors"]} MIM plates · {g["requested_capacitance_F"] * 1e12:g} pF intended storage', 25, MUTED)
    for index, name in enumerate(('Metal4', 'Metal5', 'TopMetal1', 'TopMetal2')):
        x = 70 + index * 390
        draw.rectangle((x, 1415, x + 28, 1443), fill=LAYERS[name][1])
        label(draw, (x + 40, 1410), name, 26)
    label(draw, (70, 1485), 'Layer colors identify masks. Local DRC/LVS passed; transient acceptance is pending. This is not an integrated chip.', 24, MUTED)
    canvas.save(destination / 'receive_reference_storage.png', optimize=True)


def run(args):
    proof = json.loads(args.layout_evidence.read_text())
    if (proof.get('status') != 'pass' or proof.get('stage') != 'complete' or
            proof.get('local_drc_violations') != 0 or not proof.get('missing_device_rejected') or
            not proof.get('geometry', {}).get('storage_access')):
        raise ValueError('passing storage-access geometry evidence required')
    for group in ('source_sha256', 'artifact_sha256'):
        verify(proof[group])
    gds = Path(proof['work']) / 'reference.gds'
    if proof['artifact_sha256'].get(str(gds)) != digest(gds):
        raise ValueError('layout is not the verified artifact')
    layout = db.Layout()
    layout.read(str(gds))
    tops = list(layout.top_cells())
    if len(tops) != 1 or tops[0].name != 'ReceiveCommonReference':
        raise ValueError('unexpected GDS hierarchy')
    box = tops[0].bbox()
    bounds = [v * layout.dbu for v in (box.left, box.bottom, box.right, box.top)]
    geometry = proof['geometry']
    args.work.mkdir(parents=True, exist_ok=True)
    args.output.mkdir(parents=True, exist_ok=True)
    with tempfile.TemporaryDirectory(prefix='receive-package-', dir=args.work) as directory:
        work = Path(directory)
        packed = work / 'receive_reference.gds.gz'
        with gds.open('rb') as source, packed.open('wb') as target:
            with gzip.GzipFile(filename='', mode='wb', fileobj=target, mtime=0) as zipped:
                shutil.copyfileobj(source, zipped)
        with gzip.open(packed, 'rb') as stream:
            if hashlib.file_digest(stream, 'sha256').hexdigest() != digest(gds):
                raise ValueError('compressed GDS does not reconstruct verified bytes')
        pictures(gds, work, work, geometry, bounds)
        paths = [packed, work / 'receive_reference_layers.png', work / 'receive_reference_storage.png']
        report = dict(package='receiver-reference storage-access revision',
            generated_utc=datetime.now(timezone.utc).isoformat(),
            source_base_commit=subprocess.check_output(['git', 'rev-parse', 'HEAD'], cwd=ROOT, text=True).strip(),
            source_worktree_included=True, top=tops[0].name, database_unit_um=layout.dbu,
            bounds_um=bounds, width_um=geometry['width_um'], height_um=geometry['height_um'],
            area_um2=geometry['bounding_area_um2'], gds_uncompressed_sha256=digest(gds),
            geometry_evidence=dict(path=str(args.layout_evidence.resolve()), sha256=digest(args.layout_evidence),
                status='pass', local_drc_violations=0, retained_minimum_density_violations=proof['retained_minimum_density_violations'],
                lvs={k:v for k,v in proof['lvs'].items() if k != 'report'}, missing_device_rejected=True,
                source_files_checked=len(proof['source_sha256']),
                source_manifest_sha256=hashlib.sha256(json.dumps(proof['source_sha256'], sort_keys=True).encode()).hexdigest()),
            storage_access=geometry['storage_access'], pins=geometry['terminals'],
            electrical_qualified=False, full_chip_qualified=False, fabrication_release=False,
            limitations=['Unfilled standalone receiver-reference macro, not an assembled repeater.',
                'Minimum-density rules remain for chip fill. Local geometry checks are not full-chip DRC.',
                'The repaired transient is pending; clock, transmitter and receiver integration remain open.',
                'Earlier core/padframe artifacts in this directory are separate historical layouts.'],
            rendering=dict(palette=LAYERS, mask_datatype=0, physical_geometry=True, artificial_geometry=False),
            source_files={str(p.relative_to(ROOT)):digest(p) for p in [Path(__file__).resolve()] +
                [Path(name) for name in proof['source_sha256'] if Path(name).is_relative_to(ROOT / '1G/IHPSG13G2') and '/tests/' not in name]},
            artifacts={p.name:dict(bytes=p.stat().st_size, sha256=digest(p)) for p in paths})
        metadata = work / 'receive_reference_provenance.json'
        metadata.write_text(json.dumps(report, indent=2) + '\n')
        # Recheck before publishing; an active design must never be relabeled
        # using stale verification evidence.
        for group in ('source_sha256', 'artifact_sha256'):
            verify(proof[group])
        for path in paths + [metadata]:
            shutil.copyfile(path, args.output / path.name)
        print(json.dumps(dict(status='packaged', output=str(args.output),
            artifacts=report['artifacts'], electrical_qualified=False), indent=2))


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--layout-evidence', type=Path, default=ROOT / 'build/regeneration-audit/1G/IHPSG13G2/tests/results/receive_common_storage_layout/checks.json')
    parser.add_argument('--output', type=Path, default=ROOT / '1G/IHPSG13G2/results')
    parser.add_argument('--work', type=Path, default=ROOT / 'build/receiver-package')
    run(parser.parse_args())
