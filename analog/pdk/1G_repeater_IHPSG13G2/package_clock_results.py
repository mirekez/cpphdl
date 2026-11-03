"""Publish the checked processing-clock controller and actual mask views.

Uses the original implementation's retained source snapshots when flow sources
have changed. This packages existing evidence; it does not renew signoff or
claim that the standalone controller is an integrated repeater.
"""
import argparse
from datetime import datetime, timezone
import gzip
import hashlib
import json
from pathlib import Path
import shutil
import tempfile

ROOT = Path(__file__).resolve().parents[2]


def digest(path):
    result = hashlib.sha256()
    with Path(path).open('rb') as stream:
        for chunk in iter(lambda: stream.read(1024 * 1024), b''):
            result.update(chunk)
    return result.hexdigest()


def verify_implementation(work):
    proof = json.loads((work / 'physical_module.json').read_text())
    if (proof.get('status') != 'component_checks_pass' or
            proof.get('top') != 'ProcessingClockControl' or
            proof.get('native_postroute_exact') is not True or
            proof.get('full_1000base_t_chip') is not False):
        raise ValueError('completed standalone clock-controller evidence required')
    if not proof.get('input_sha256') or not proof.get('artifact_sha256'):
        raise ValueError('missing implementation provenance')
    restored = {}
    unavailable_executables = {}
    for name, expected in proof['input_sha256'].items():
        path = Path(name)
        candidates = [path]
        if name in proof.get('retained_input_paths', {}):
            candidates.append(Path(proof['retained_input_paths'][name]))
        if path.is_relative_to(ROOT.parent):
            candidates.append(work / 'source_snapshot' / path.relative_to(ROOT.parent))
        retained = next((p for p in candidates if p.is_file() and digest(p) == expected), None)
        if retained is None:
            # The build directory's native executable is overwritten by later
            # builds. Its original source and recorded native/routed traces
            # remain mandatory; packaging does not rerun or qualify that binary.
            if path == ROOT / 'build/1G/tests/analog_1g_processing_clock_control':
                unavailable_executables[name] = expected
                continue
            raise ValueError('original implementation input unavailable: ' + name)
        if retained != path:
            restored[name] = str(retained)
    for name, expected in proof['artifact_sha256'].items():
        path = work / name
        if not path.is_file() or digest(path) != expected:
            raise ValueError('verified physical artifact changed: ' + name)
    if 'module_filled.gds' not in proof['artifact_sha256']:
        raise ValueError('filled GDS is not bound to the implementation')
    native = work / 'cpp_trace.csv'
    originals = [name for name, retained in proof.get('retained_input_paths', {}).items()
                 if Path(retained) == native]
    if (len(originals) != 1 or proof['input_sha256'].get(originals[0]) != digest(native) or
            digest(native) != digest(work / 'postroute_trace.csv')):
        raise ValueError('recorded native/routed trajectories differ or are unbound')
    return proof, restored, unavailable_executables


def run(args):
    # Rendering libraries are not needed for the provenance regression.
    import klayout.db as db
    from PIL import Image, ImageDraw
    from package_receive_results import render, label, scale, LAYERS, BACKGROUND, ACCENT, MUTED
    from checks import check_timing, check_drc, check_lvs

    implementation = args.implementation.resolve()
    proof, restored, unavailable_executables = verify_implementation(implementation)
    timing = check_timing(implementation)
    if timing != proof['timing']:
        raise ValueError('saved routed timing differs from its receipt')
    drc = check_drc(Path(proof['drc']['report']))
    if drc != proof['drc']:
        raise ValueError('saved full-deck DRC differs from its receipt')
    # The signal interface comes from the exact netlist bound to the receipt.
    import sys
    sys.path.insert(0, str(ROOT / '1G/IHPSG13G2'))
    from buffer_equivalence import parse
    ports = {'VDD', 'VSS'} | {name if bit is None else f'{name}[{bit}]'
                            for name, bit in parse(implementation / 'postroute.v')['ports']}
    lvs = check_lvs(Path(proof['lvs']['report']), proof['top'], ports)
    if lvs != proof['lvs']:
        raise ValueError('saved transistor LVS differs from its receipt')
    gds = implementation / 'module_filled.gds'
    layout = db.Layout()
    layout.read(str(gds))
    tops = list(layout.top_cells())
    if len(tops) != 1 or tops[0].name != proof['top']:
        raise ValueError('unexpected GDS hierarchy')
    box = tops[0].bbox()
    bounds = [v * layout.dbu for v in (box.left, box.bottom, box.right, box.top)]
    if bounds != proof['stream']['bbox_um'] or layout.dbu != proof['stream']['dbu_um']:
        raise ValueError('GDS geometry differs from stream evidence')
    args.work.mkdir(parents=True, exist_ok=True)
    args.output.mkdir(parents=True, exist_ok=True)
    with tempfile.TemporaryDirectory(prefix='clock-package-', dir=args.work) as directory:
        work = Path(directory)
        packed = work / 'processing_clock_control.gds.gz'
        with gds.open('rb') as source, packed.open('wb') as target:
            with gzip.GzipFile(filename='', mode='wb', fileobj=target, mtime=0) as zipped:
                shutil.copyfileobj(source, zipped)
        with gzip.open(packed, 'rb') as stream:
            if hashlib.file_digest(stream, 'sha256').hexdigest() != digest(gds):
                raise ValueError('compressed GDS does not reproduce the checked layout')
        canvas = Image.new('RGB', (2000, 1510), BACKGROUND)
        draw = ImageDraw.Draw(canvas)
        label(draw, (55, 30), 'Processing clock controller / routed IHP SG13G2 macro', 37)
        label(draw, (55, 86), '280 × 280 µm · 800 MHz acquisition / 125 MHz reference · actual GDSII masks', 24, ACCENT)
        panels = [('Devices and lower routing', list(LAYERS)[:9]),
                  ('Upper routing', list(LAYERS)[9:]),
                  ('Complete drawing-layer view', list(LAYERS))]
        for i, (title, selected) in enumerate(panels):
            x = 55 + i * 650
            label(draw, (x, 155), title, 24)
            tile, ratio = render(gds, work, f'layer-{i}', selected, [-4, -4, 284, 284], 600, 600)
            canvas.paste(tile, (x, 205))
            scale(draw, x + 20, 835, ratio, 50)
        lines = [
            'Checked standalone implementation',
            f'Extracted setup: {timing["setup_slack_ns"]:+.6f} ns    Hold: {timing["hold_slack_ns"]:+.6f} ns',
            'Ready output load: 80 fF · exact native/routed replay: 292,092 events',
            f'Full-deck DRC: {drc["violations"]} violations · transistor LVS: {lvs["instantiated_devices"]:,} devices',
            'Original nominal routed RC; post-fill coupling is not extracted.',
            'Standalone controller. Receiver clock-tree integration and complete-chip checks remain open.',
            'Colors identify physical masks. Density fill is present in the GDS; views show drawing layers.',
        ]
        for i, line in enumerate(lines):
            label(draw, (55, 960 + i * 63), line, 27 if i == 0 else 24, ACCENT if i == 0 else MUTED)
        picture = work / 'processing_clock_control_layers.png'
        canvas.save(picture, optimize=True)
        files = [packed, picture]
        record = dict(packaged_utc=datetime.now(timezone.utc).isoformat(),
            scope='Existing standalone controller implementation; not a new integrated ASIC',
            top=proof['top'], bounds_um=bounds, database_unit_um=layout.dbu,
            implementation_receipt=dict(path=str(implementation / 'physical_module.json'),
                sha256=digest(implementation / 'physical_module.json')),
            gds_uncompressed_sha256=digest(gds), timing=timing, drc=drc, lvs=lvs,
            constraints=proof['constraints'], native_postroute_exact=True,
            original_input_sha256=proof['input_sha256'], retained_input_paths=restored,
            unavailable_original_executables=unavailable_executables,
            functional_evidence='Original source hashes and recorded native/routed traces rechecked; no simulation rerun',
            physical_artifact_sha256=proof['artifact_sha256'],
            packaging_source_sha256={str(p): digest(p) for p in
                [Path(__file__).resolve(), Path(__file__).with_name('package_receive_results.py'),
                 Path(__file__).with_name('checks.py')]},
            postfill_timing_qualified=False, full_chip_qualified=False, fabrication_release=False,
            artifacts={p.name: dict(bytes=p.stat().st_size, sha256=digest(p)) for p in files})
        metadata = work / 'processing_clock_control_provenance.json'
        metadata.write_text(json.dumps(record, indent=2) + '\n')
        verify_implementation(implementation)
        for path in files + [metadata]:
            shutil.copyfile(path, args.output / path.name)
        print(json.dumps(dict(status='packaged', artifacts=record['artifacts']), indent=2))


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--implementation', type=Path, required=True)
    parser.add_argument('--output', type=Path, default=ROOT / '1G/IHPSG13G2/results')
    parser.add_argument('--work', type=Path, default=ROOT / 'build/clock-package')
    run(parser.parse_args())
