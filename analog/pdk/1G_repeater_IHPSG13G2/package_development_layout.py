#!/usr/bin/env python3
"""Publish current physical checkpoints, including failures, without signoff gates.

A combined review layout preserves each block in a separate namespace and places
blocks without overlap. It adds no electrical connections. Recorded verification
belongs to the input block; assembly DRC/LVS/timing must be performed separately.
Run with the PDK Python and Pillow (PYTHONPATH=out/pdf-renderer locally).
"""
import argparse
import gzip
import hashlib
import json
import os
from pathlib import Path
import re
import shutil
import subprocess
import tempfile
from datetime import datetime, timezone

import klayout.db as db
from PIL import Image, ImageDraw

from package_receive_results import BACKGROUND, ACCENT, MUTED, LAYERS, label, render

ROOT = Path(__file__).resolve().parents[2]
HERE = Path(__file__).resolve().parent


def digest(path):
    value = hashlib.sha256()
    with Path(path).open('rb') as stream:
        for chunk in iter(lambda: stream.read(1024 * 1024), b''):
            value.update(chunk)
    return value.hexdigest()


def resolve(path):
    path = Path(path)
    return path if path.is_absolute() else ROOT / path


def relative(path):
    try:
        return str(Path(path).resolve().relative_to(ROOT.resolve()))
    except ValueError:
        return str(Path(path).resolve())


def verify(hashes):
    for name, expected in hashes.items():
        if digest(resolve(name)) != expected:
            raise ValueError('publication input changed: ' + name)


def hierarchy_digest(top):
    """Hash physical geometry and hierarchy, independent of cell names/order."""
    layout = top.layout()
    cache = {}

    def visit(cell):
        if cell.cell_index() in cache:
            return cache[cell.cell_index()]
        h = hashlib.sha256()
        for layer in sorted(layout.layer_indices(), key=lambda i: layout.get_info(i).to_s()):
            shapes = sorted(shape.to_s() for shape in cell.shapes(layer).each())
            if shapes:
                h.update((layout.get_info(layer).to_s() + '\n').encode())
                for shape in shapes:
                    h.update((shape + '\n').encode())
        instances = []
        for instance in cell.each_inst():
            instances.append((visit(instance.cell), instance.cplx_trans.to_s(),
                              instance.a.to_s(), instance.b.to_s(), instance.na, instance.nb))
        h.update(json.dumps(sorted(instances)).encode())
        cache[cell.cell_index()] = h.hexdigest()
        return cache[cell.cell_index()]
    return visit(top)


def read_block(path, name):
    layout = db.Layout()
    layout.read(str(path))
    top = layout.cell(name)
    if top is None or top.is_empty() or top.bbox().empty():
        raise ValueError('missing or empty physical top: ' + name)
    if abs(layout.dbu - 0.001) > 1e-12:
        raise ValueError('expected 1 nm geometry; refusing implicit rescaling')
    return layout, top


def export_checkpoint(entry, work, pdk, tools, hashes):
    source = resolve(entry['source'])
    hashes[relative(source)] = digest(source)
    kind = entry.get('format', 'gds')
    if kind == 'gds':
        return source
    work.mkdir(parents=True, exist_ok=True)
    if kind == 'odb':
        script = work / 'export.tcl'
        script.write_text('set_thread_count 1\nread_db $::env(PUBLISH_ODB)\n'
                          'write_def $::env(PUBLISH_DEF)\nexit\n')
        env = dict(os.environ, PUBLISH_ODB=str(source), PUBLISH_DEF=str(work / 'core.def'))
        with (work / 'export.log').open('w') as log:
            subprocess.run([str(tools / 'bin/openroad'), '-no_init', '-exit', str(script)],
                           env=env, stdout=log, stderr=subprocess.STDOUT, check=True, timeout=120)
        for executable in ('bin/openroad', 'root/usr/bin/openroad'):
            hashes[relative(tools / executable)] = digest(tools / executable)
    elif kind == 'def':
        opener = gzip.open if source.suffix == '.gz' else open
        with opener(source, 'rb') as src, (work / 'core.def').open('wb') as dst:
            shutil.copyfileobj(src, dst)
    else:
        raise ValueError('unknown checkpoint format: ' + kind)
    from route_tech import prepare
    from stream import stream
    prepare(pdk, work)
    stream(pdk, work, entry['top'])
    for path in (HERE / 'route_tech.py', HERE / 'stream.py',
                 pdk / 'ihp-sg13g2/libs.ref/sg13g2_stdcell/gds/sg13g2_stdcell.gds',
                 pdk / 'ihp-sg13g2/libs.ref/sg13g2_stdcell/lef/sg13g2_stdcell.lef',
                 pdk / 'ihp-sg13g2/libs.ref/sg13g2_stdcell/lef/sg13g2_tech.lef',
                 pdk / 'ihp-sg13g2/libs.tech/klayout/tech/sg13g2.lyt',
                 pdk / 'ihp-sg13g2/libs.tech/klayout/tech/sg13g2.map'):
        hashes[relative(path)] = digest(path)
    return work / 'repeater_core_unfilled.gds'


def evidence(entry, source_hash, hashes):
    records = []
    for name in entry.get('receipts', []):
        path = resolve(name)
        hashes[relative(path)] = digest(path)
        data = json.loads(path.read_text())
        # Saved receipts can describe failing candidates. Keep that status and
        # bind it to bytes where the producer recorded an artifact hash.
        def contains(value):
            if isinstance(value, dict):
                return any(contains(v) for v in value.values())
            if isinstance(value, list):
                return any(contains(v) for v in value)
            return value == source_hash
        keys = ('status', 'stage', 'error', 'timing', 'setup_ns', 'hold_ns',
                'timing_qualified', 'physical_qualified', 'geometry_qualified',
                'drc_qualified', 'lvs_qualified', 'full_chip_qualified',
                'local_drc_violations', 'drc', 'lvs', 'electrical_pass',
                'global_electrical_pass', 'global_estimates', 'native_postroute_exact')
        records.append(dict(path=relative(path), sha256=digest(path),
            source_hash_recorded=contains(data), recorded={k: data[k] for k in keys if k in data}))
    return records


def pictures(gds, top_name, bounds, work, output, records):
    canvas = Image.new('RGB', (2400, 1490), BACKGROUND)
    draw = ImageDraw.Draw(canvas)
    label(draw, (50, 25), '1G repeater / current physical block assembly', 38)
    label(draw, (50, 78), 'Actual mask geometry · separate block placements · inter-block routing unfinished', 23, ACCENT)
    groups = [('All drawing layers', list(LAYERS)),
              ('Devices and lower metals', list(LAYERS)[:9]),
              ('Upper metals', list(LAYERS)[9:])]
    for index, (title, layers) in enumerate(groups):
        x = 50 + index * 780
        label(draw, (x, 135), title, 24)
        tile, ratio = render(gds, work, f'assembly-{index}', layers, bounds, 740, 800)
        canvas.paste(tile, (x, 180))
    for index, record in enumerate(records):
        column, row = index // 11, index % 11
        label(draw, (50 + column * 1170, 1035 + row * 30),
              f'{index + 1:02d}  {record["title"]}', 23, MUTED)
    label(draw, (50, 1400), f'{len(records)} blocks · {bounds[2]-bounds[0]:.1f} × {bounds[3]-bounds[1]:.1f} µm review arrangement; not a die-size claim', 24, MUTED)
    label(draw, (50, 1445), 'Timing/DRC/LVS failures and untested connections remain recorded. See README and provenance.', 23, MUTED)
    canvas.save(output / 'repeater_layers.png', optimize=True)


def run(args):
    selection = json.loads(args.selection.read_text())
    entries = selection['blocks']
    names = [entry['name'] for entry in entries]
    if not entries or len(names) != len(set(names)) or any(not re.fullmatch(r'[a-z][a-z0-9_]*', n) for n in names):
        raise ValueError('unique safe block names required')
    args.work.mkdir(parents=True, exist_ok=True)
    args.output.mkdir(parents=True, exist_ok=True)
    hashes = {relative(args.selection): digest(args.selection), relative(__file__): digest(__file__),
              relative(HERE / 'package_receive_results.py'): digest(HERE / 'package_receive_results.py')}
    combined = db.Layout()
    combined.dbu = 0.001
    assembly = combined.create_cell('RepeaterDevelopmentAssembly')
    records = []
    x, y, row_height = 0, 0, 0
    with tempfile.TemporaryDirectory(prefix='publish-', dir=args.work.resolve()) as temporary:
        work = Path(temporary)
        for index, entry in enumerate(entries):
            print('Publishing', entry['name'], flush=True)
            local = work / entry['name']
            local.mkdir()
            source = resolve(entry['source'])
            gds = export_checkpoint(entry, local, args.pdk.resolve(), args.tools.resolve(), hashes)
            layout, top = read_block(gds, entry['top'])
            original = hierarchy_digest(top)
            prefix = f'B{index:02d}_'
            # Namespace every cell; different PDK/macro revisions must never
            # silently overwrite cells with the same name during a merge.
            destination = combined.create_cell(prefix + entry['top'])
            destination.copy_tree(top)
            for cell in list(combined.each_cell()):
                if cell.cell_index() == assembly.cell_index() or cell.name.startswith('B') and re.match(r'B\d\d_', cell.name):
                    continue
                cell.name = prefix + cell.name
            if hierarchy_digest(destination) != original:
                raise ValueError('geometry changed during merge: ' + entry['name'])
            box = destination.bbox()
            width, height = box.width(), box.height()
            gap, row_width = 100000, 6000000
            if x and x + width > row_width:
                x, y, row_height = 0, y + row_height + gap, 0
            dx, dy = x - box.left, y - box.bottom
            assembly.insert(db.CellInstArray(destination.cell_index(), db.Trans(dx, dy)))
            placed = [x * .001, y * .001, (x + width) * .001, (y + height) * .001]
            x += width + gap
            row_height = max(row_height, height)
            record = dict(entry, assembly_cell=destination.name, source_sha256=hashes[relative(source)],
                geometry_sha256=original, bounds_um=placed, width_um=width * .001, height_um=height * .001,
                evidence=evidence(entry, hashes[relative(source)], hashes))
            records.append(record)
            # One PNG per block is useful even when only the combined GDS ships.
            bounds = [v * layout.dbu for v in (top.bbox().left, top.bbox().bottom, top.bbox().right, top.bbox().top)]
            tile, _ = render(gds, local, 'masks', list(LAYERS), bounds, 1440, 900)
            canvas = Image.new('RGB', (1500, 1080), BACKGROUND)
            draw = ImageDraw.Draw(canvas)
            label(draw, (30, 20), entry['title'], 30)
            label(draw, (30, 65), f'{width*.001:.2f} × {height*.001:.2f} µm · {entry["top"]}', 22, ACCENT)
            canvas.paste(tile, (30, 110))
            label(draw, (30, 1020), entry['caption'], 20, MUTED)
            canvas.save(work / (entry['name'] + '.png'), optimize=True)
            del top, layout
        packed = work / 'repeater_development.gds.gz'
        combined.write(str(packed))
        check, top = read_block(packed, assembly.name)
        if len(list(check.top_cells())) != 1 or len(list(top.each_inst())) != len(entries):
            raise ValueError('incomplete combined hierarchy')
        for record in records:
            if hierarchy_digest(check.cell(record['assembly_cell'])) != record['geometry_sha256']:
                raise ValueError('stream round-trip changed block geometry: ' + record['name'])
        for i, a in enumerate(records):
            for b in records[i+1:]:
                aa, bb = a['bounds_um'], b['bounds_um']
                if aa[0] < bb[2] and aa[2] > bb[0] and aa[1] < bb[3] and aa[3] > bb[1]:
                    raise ValueError('review placements overlap')
        bounds = [v * check.dbu for v in (top.bbox().left, top.bbox().bottom, top.bbox().right, top.bbox().top)]
        pictures(packed, assembly.name, bounds, work, work, records)
        report = dict(schema=1, generated_utc=datetime.now(timezone.utc).isoformat(),
            source_base_commit=subprocess.check_output(['git', 'rev-parse', 'HEAD'], cwd=ROOT, text=True).strip(),
            status='development_snapshot', top=assembly.name, full_chip_qualified=False,
            electrically_integrated=False, inter_block_connections_added=0,
            scope='Non-overlapping physical block collection; not an electrically connected repeater.',
            database_unit_um=check.dbu, bounds_um=bounds, blocks=records, input_sha256=hashes,
            checks=dict(block_geometry_preserved=True, gds_round_trip=True, nonoverlapping_blocks=True,
                        single_top=True, simulations_rerun=False, full_chip_drc_run=False,
                        full_chip_lvs_run=False, full_chip_timing_run=False))
        outputs = [packed, work / 'repeater_layers.png'] + [work / (n + '.png') for n in names]
        report['artifacts'] = {p.name: dict(sha256=digest(p), bytes=p.stat().st_size) for p in outputs}
        (work / 'development_provenance.json').write_text(json.dumps(report, indent=2) + '\n')
        lines = ["# Current 1G physical layouts", "",
            "One combined GDSII contains the current available physical blocks, including failed and unfinished candidates.",
            "**This is a development assembly, not an electrically integrated or fabrication-qualified repeater.**",
            "The merge adds no signal, clock or power connections between blocks. Separate clock alternatives and standalone constituent blocks are included for review.", "",
            "- [Combined GDSII](repeater_development.gds.gz): open `RepeaterDevelopmentAssembly` in KLayout.",
            "- [Layer overview](repeater_layers.png): actual drawing-layer masks, regenerated from that GDSII.",
            "- [Provenance and recorded checks](development_provenance.json): source hashes, status, placements and namespace mapping.", "",
            "![Current layout layers](repeater_layers.png)", "",
            "## Current contents", "",
            "The receiver export uses the latest completed pin-repair candidate, even though its timing screen fails. Global-route guides are not converted into metal: only geometry actually present in the OpenROAD checkpoint is exported. The receiver therefore contains placement/CTS/power geometry and still needs detailed signal routing.",
            "The PCS includes soft branch metrics and the trellis decoder. The receive equalizer/echo-canceller DSP, complete ADC/AFE and complete transmitter do not yet have integrated layouts in this package; their separate available physical constituents are listed below.",
            "The old forwarding core and padframe have no newer physical revisions, so their latest available October 24 geometry is retained. The padframe opening is too small for this collection. The combined bounding box is a review arrangement, not a proposed die size.", "",
            "| ID / cell in combined GDS | Current module view | Recorded state |",
            "| --- | --- | --- |"]
        for index, record in enumerate(records):
            lines.append(f"| {index + 1:02d} / `{record['assembly_cell']}` | [{record['title']}]({record['name']}.png) | {record['caption']} |")
        lines += ["", "## Per-module details", ""]
        for record in records:
            lines += [f"### {record['title']}", "", record['status'], "",
                      f"![{record['title']}]({record['name']}.png)", ""]
        lines += ["## What publication checks", "",
            "The publisher checks source hashes, rejects missing or empty tops, namespaces cells, preserves each block's hierarchical geometry through merge and compressed-GDS round trip, and checks that block bounding boxes do not overlap. Saved verification statuses are retained, including failures; packaging does not rerun electrical simulation, timing, DRC or LVS. A historical passing block receipt does not qualify the assembled layout.", "",
            "Only the combined GDS, current module/overview PNGs, compact provenance and license are published. Build checkpoints, generated HDL and raw logs remain outside Git.", "",
            "## Regeneration", "",
            "From `analog/`, with the original checkpoints listed in the provenance available:", "",
            "```sh",
            "PYTHONPATH=out/pdf-renderer pdk/IHPSG13G2/.cpphdl/venv/bin/python pdk/1G_repeater_IHPSG13G2/package_development_layout.py --selection 1G/IHPSG13G2/results/development_provenance.json",
            "ctest --test-dir build/behavior -R '^analog.pdk.1g_repeater.package_development_layout$' --output-on-failure",
            "```", "",
            "The selection records the source checkpoint and producer receipt for every block. This command republishes existing checkpoints; it does not recreate the PnR runs. The retained forwarding-core/padframe inputs and their historical provenance can be recovered from commit `ea6cf21`, under `analog/1G/IHPSG13G2/results/`, into `build/layout-publication/retained-inputs/`.", "",
            "The earlier standalone GDS files and images remain available in Git history. Their current available geometry is included here.", "",
            "## Attribution", "",
            "Embedded IHP PDK cell geometry is distributed under [Apache-2.0](IHP-LICENSE.txt). Custom circuit layout, assembly and publication tooling are cpphdl project work.", ""]
        (work / 'README.md').write_text('\n'.join(lines))

        verify(hashes)
        for path in outputs + [work / 'development_provenance.json', work / 'README.md']:
            shutil.copyfile(path, args.output / path.name)
        print(json.dumps(dict(blocks=len(entries), bytes=sum(p.stat().st_size for p in outputs),
                              gds=str(args.output / packed.name)), indent=2))


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--selection', type=Path, required=True,
                        help='JSON with blocks; published provenance can also be reused')
    parser.add_argument('--output', type=Path, default=ROOT / '1G/IHPSG13G2/results')
    parser.add_argument('--work', type=Path, default=ROOT / 'build/layout-publication')
    parser.add_argument('--pdk', type=Path, default=ROOT / 'pdk/IHPSG13G2')
    parser.add_argument('--tools', type=Path, default=ROOT / 'out/repeater-toolchain')
    run(parser.parse_args())
