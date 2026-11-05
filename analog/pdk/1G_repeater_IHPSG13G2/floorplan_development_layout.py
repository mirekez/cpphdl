#!/usr/bin/env python3
"""Place the published candidates inside one real, enlarged IHP padframe.

This preserves each interior block, including its recorded failures. It is a
floorplan of available candidates, not a connected production repeater netlist.
"""
import argparse
from copy import deepcopy
from datetime import datetime, timezone
import json
import math
from pathlib import Path
import shutil
import tempfile

import klayout.db as db
from PIL import Image, ImageDraw

from padframe import assemble
from package_development_layout import (ROOT, digest, evidence, export_checkpoint,
                                        hierarchy_digest, pictures, read_block, relative)
from package_receive_results import ACCENT, BACKGROUND, LAYERS, MUTED, label, render
from render_development_layout import run as render_full

HERE = Path(__file__).resolve().parent
PAD = 'padframe_current'


def update_candidates(source, records, replacements, work, pdk, tools):
    """Replace complete candidates, retaining source hashes and failure evidence."""
    records = deepcopy(records)
    hashes = {}
    changes = []
    for replacement in replacements:
        entry = replacement['block']
        previous = set(replacement['replaces'])
        names = {record['name'] for record in records}
        if previous <= names:
            removed = previous
        elif not (previous - {entry['name']}) & names and entry['name'] in names:
            # Re-running the same reviewed plan is allowed after publication.
            removed = {entry['name']}
        else:
            raise ValueError('incomplete replacement inventory: ' + entry['name'])
        if entry['name'] in names - removed or PAD in removed:
            raise ValueError('replacement collides with retained inventory')
        gds = export_checkpoint(entry, work / entry['name'], pdk, tools, hashes)
        imported, top = read_block(gds, entry['top'])
        geometry = hierarchy_digest(top)
        cell = source.create_cell('UPDATED_' + entry['top'])
        cell.copy_tree(top)
        if hierarchy_digest(cell) != geometry:
            raise ValueError('replacement geometry changed during import')
        box = cell.dbbox()
        source_hash = hashes[relative(entry['source'])]
        record = dict(entry, assembly_cell=cell.name, source_sha256=source_hash,
                      geometry_sha256=geometry, width_um=box.width(), height_um=box.height(),
                      evidence=evidence(entry, source_hash, hashes))
        changes.append(dict(name=entry['name'], replaces=sorted(previous),
                            source_sha256=source_hash, geometry_sha256=geometry,
                            exported_gds=str(gds)))
        index = min(i for i, old in enumerate(records) if old['name'] in removed)
        records = [old for old in records if old['name'] not in removed]
        records.insert(index, record)
        imported.destroy()
    return records, hashes, changes


def plan_blocks(records, plan, origin):
    """Fail closed if a revised block no longer fits the reviewed placements."""
    names = [b['name'] for b in records if b['name'] != PAD]
    if len(names) != len(set(names)) or set(names) != set(plan['positions_um']):
        raise ValueError('floorplan inventory differs from available blocks')
    size, margin, gap = (plan[k] for k in ('core_opening_um', 'core_margin_um', 'channel_um'))
    if (isinstance(size, bool) or not isinstance(size, int) or size < 640
            or not math.isfinite(margin) or not math.isfinite(gap) or min(margin, gap) <= 0):
        raise ValueError('invalid core or clearance')
    result = []
    for block in records:
        if block['name'] == PAD:
            continue
        x, y = plan['positions_um'][block['name']]
        w, h = block['width_um'], block['height_um']
        if not all(math.isfinite(v) for v in (x, y, w, h)) or min(w, h) <= 0:
            raise ValueError('invalid block dimensions')
        if min(x, y) < margin or max(x + w, y + h) > size - margin + 1e-6:
            raise ValueError('block exceeds core margin: ' + block['name'])
        placed = [x + origin[0], y + origin[1], x + w + origin[0], y + h + origin[1]]
        for other in result:
            a, b = placed, other['bounds_um']
            if max(b[0] - a[2], a[0] - b[2], b[1] - a[3], a[1] - b[3]) < gap - 1e-6:
                raise ValueError('routing channel collision: ' + block['name'] + '/' + other['name'])
        result.append(dict(deepcopy(block), bounds_um=placed))
    return result


def compose(source, records, pad_layout, pad_report, plan):
    pads = [b for b in records if b['name'] == PAD]
    if len(pads) != 1:
        raise ValueError('exactly one replaceable padframe is required')
    placed = plan_blocks(records, plan, pad_report['core_bbox_um'][:2])
    if abs(source.dbu - .001) > 1e-12 or abs(pad_layout.dbu - source.dbu) > 1e-12:
        raise ValueError('expected matching 1 nm geometry')
    layout = db.Layout()
    layout.dbu = source.dbu
    top = layout.create_cell('RepeaterDevelopmentDie')
    footprints = []
    for block in placed:
        original = source.cell(block['assembly_cell'])
        if original is None or hierarchy_digest(original) != block['geometry_sha256']:
            raise ValueError('source block geometry changed: ' + block['name'])
        if (abs(original.dbbox().width() - block['width_um']) > source.dbu
                or abs(original.dbbox().height() - block['height_um']) > source.dbu):
            raise ValueError('block dimensions differ from inventory: ' + block['name'])
        cell = layout.create_cell(original.name)
        cell.copy_tree(original)
        bounds = cell.bbox()
        x, y = block['bounds_um'][:2]
        trans = db.Trans(round(x / layout.dbu) - bounds.left, round(y / layout.dbu) - bounds.bottom)
        top.insert(db.CellInstArray(cell.cell_index(), trans))
        footprints.append(bounds.transformed(trans))
        block['placement_um'] = [trans.disp.x * layout.dbu, trans.disp.y * layout.dbu]
    pad = layout.create_cell(pads[0]['assembly_cell'])
    pad.copy_tree(pad_layout.cell(pad_report['top']))
    top.insert(db.CellInstArray(pad.cell_index(), db.Trans()))
    # Check the actual sealring, pad cells, filler and decap geometry, not the
    # padframe bounding rectangle (which deliberately encloses the whole core).
    for layer in layout.layer_indices():
        # E.g. EdgeSeal.boundary (39/4) is a die-sized annotation rectangle,
        # whereas EdgeSeal.drawing (39/0) is the physical perimeter ring.
        if layout.get_info(layer).datatype != 0:
            continue
        for footprint in footprints:
            # Spatial queries avoid flattening the entire pad/seal hierarchy
            # just to check an otherwise empty core opening.
            shapes = db.Region(pad.begin_shapes_rec_overlapping(layer, footprint))
            if not (shapes & db.Region(footprint)).is_empty():
                raise ValueError('padframe intrudes into module footprint on ' + layout.get_info(layer).to_s())
    return layout, top, placed, pad


def run(args):
    if not 2000 <= args.width <= 16000:
        raise ValueError('width must be between 2000 and 16000 pixels')
    provenance = args.provenance.resolve()
    source_gds = provenance.parent / 'repeater_development.gds.gz'
    report = json.loads(provenance.read_text())
    source_hash, provenance_hash, plan_hash = digest(source_gds), digest(provenance), digest(args.plan)
    if source_hash != report['artifacts'][source_gds.name]['sha256']:
        raise ValueError('source GDS differs from publication provenance')
    plan = json.loads(args.plan.read_text())
    source, source_top = read_block(source_gds, report['top'])
    if len(source.top_cells()) != 1 or sorted(i.cell.name for i in source_top.each_inst()) != sorted(
            b['assembly_cell'] for b in report['blocks']):
        raise ValueError('source hierarchy differs from inventory')
    args.work.mkdir(parents=True, exist_ok=True)
    # Keep one immutable input snapshot so in-place publication is reproducible.
    snapshot = args.work / ('input-' + source_hash[:16] + '-' + provenance_hash[:16])
    snapshot.mkdir(exist_ok=True)
    for path in (source_gds, provenance):
        destination = snapshot / path.name
        if not destination.exists():
            shutil.copy2(path, destination)
        elif digest(destination) != digest(path):
            raise ValueError('immutable input snapshot differs')
    records, replacement_hashes, replacements = update_candidates(
        source, report['blocks'], plan.get('replacements', []), args.work / 'replacements',
        args.pdk.resolve(), args.tools.resolve())
    plan_blocks(records, plan, [0, 0])
    pad_work = args.work / ('padframe-' + str(plan['core_opening_um']))
    print('Generating enlarged IHP I/O padframe and sealring', flush=True)
    pad_report = assemble(args.pdk.resolve(), pad_work.resolve(), core_size=plan['core_opening_um'])
    pad_layout, _ = read_block(pad_work / 'repeater_padframe.gds', pad_report['top'])
    for cell in pad_layout.each_cell():
        cell.name = 'DIE_IO_' + cell.name
    print('Placing preserved blocks and checking clearances', flush=True)
    layout, top, placed, pad = compose(source, records, pad_layout,
                                     dict(pad_report, top='DIE_IO_' + pad_report['top']), plan)
    source.destroy()
    pad_layout.destroy()
    top_name = top.name
    die = pad_report['die_um']
    bounds = [0, 0, die, die]
    if top.dbbox() != db.DBox(*bounds):
        raise ValueError('geometry exceeds proposed die boundary')
    old_pad = next(b for b in report['blocks'] if b['name'] == PAD)
    new_pad = dict(name=PAD, top='RepeaterPadframe', assembly_cell=pad.name,
        title='Shared I/O padframe / enlarged', caption='Real IHP pads, fillers and sealring; core wiring pending.',
        status='Geometric assembly checks pass. Full-chip DRC, LVS, supply integrity and ESD qualification pending.',
        source=str(pad_work / 'repeater_padframe.gds'), format='gds',
        source_sha256=digest(pad_work / 'repeater_padframe.gds'), geometry_sha256=hierarchy_digest(pad),
        bounds_um=bounds, width_um=die, height_um=die,
        receipts=[str(pad_work / 'padframe.json')],
        replaces=dict(source=old_pad['source'], source_sha256=old_pad['source_sha256']))
    by_name = {b['name']: b for b in placed + [new_pad]}
    records = [by_name[b['name']] for b in records]
    with tempfile.TemporaryDirectory(prefix='single-die-', dir=args.work) as temporary:
        stage = Path(temporary)
        packed = stage / source_gds.name
        options = db.SaveLayoutOptions()
        options.write_context_info = False
        layout.write(str(packed), options)
        layout.destroy()
        check, checked_top = read_block(packed, top_name)
        if len(check.top_cells()) != 1 or sorted(i.cell.name for i in checked_top.each_inst()) != sorted(b['assembly_cell'] for b in records):
            raise ValueError('GDS round trip changed instance inventory')
        for block in records:
            if hierarchy_digest(check.cell(block['assembly_cell'])) != block['geometry_sha256']:
                raise ValueError('GDS round trip changed block geometry: ' + block['name'])
        check.destroy()
        result = deepcopy(report)
        result.update(generated_utc=datetime.now(timezone.utc).isoformat(),
            status='development_floorplan', top=top_name, bounds_um=bounds, blocks=records,
            full_chip_qualified=False, electrically_integrated=False, inter_block_connections_added=0,
            scope='Single-die placement of as-is candidates with shared I/O padframe. Inter-block wiring unfinished.',
            floorplan=dict(die_um=die, core_opening_um=plan['core_opening_um'], core_bbox_um=pad_report['core_bbox_um'],
                core_margin_um=plan['core_margin_um'], channel_um=plan['channel_um'],
                plan=str(args.plan.resolve()), plan_sha256=plan_hash,
                original_gds_sha256=source_hash, original_provenance_sha256=provenance_hash,
                original_snapshot=str(snapshot), padframe=pad_report,
                candidate_replacements=replacements,
                module_footprint_utilization_percent=100 * sum(
                    b['width_um'] * b['height_um'] for b in placed) / plan['core_opening_um'] ** 2,
                production_instance_inventory=False),
            checks=dict(interior_geometry_preserved=True, gds_round_trip=True, single_top=True,
                nonoverlapping_interior_blocks=True, minimum_channels_pass=True, core_containment_pass=True,
                padframe_clearance_pass=True, padframe_rails_abut=True, sealring_closed=True,
                simulations_rerun=False, full_chip_drc_run=False, full_chip_lvs_run=False, full_chip_timing_run=False))
        result['input_sha256'].update({str(args.plan.resolve()): plan_hash,
            str(Path(__file__).resolve()): digest(Path(__file__)), **pad_report['source_sha256'],
            **replacement_hashes})
        print('Rendering single-die layer overview and high-resolution PNG', flush=True)
        pictures(packed, top_name, bounds, stage, stage, records, floorplan=True)
        replacement_images = []
        for change in replacements:
            block = by_name[change['name']]
            if not block.get('render_png'):
                continue
            local, local_top = read_block(Path(change['exported_gds']), block['top'])
            box = local_top.dbbox()
            local_bounds = [box.left, box.bottom, box.right, box.top]
            local.destroy()
            tile, _ = render(Path(change['exported_gds']), stage, block['name'],
                             list(LAYERS), local_bounds, 1440, 900)
            canvas = Image.new('RGB', (1500, 1080), BACKGROUND)
            draw = ImageDraw.Draw(canvas)
            label(draw, (30, 20), block['title'], 30)
            label(draw, (30, 65), f'{block["width_um"]:.2f} x {block["height_um"]:.2f} um', 22, ACCENT)
            canvas.paste(tile, (30, 110))
            label(draw, (30, 1020), block['caption'], 20, MUTED)
            filename = block['name'] + '.png'
            canvas.save(stage / filename, optimize=True)
            replacement_images.append(filename)
        pad_tile, _ = render(pad_work / 'repeater_padframe.gds', stage, 'pad-view', list(LAYERS), bounds, 1440, 1440)
        pad_tile.save(stage / 'padframe_current.png', optimize=True)
        for name in (packed.name, 'repeater_layers.png', 'padframe_current.png', *replacement_images):
            result['artifacts'][name] = dict(sha256=digest(stage / name), bytes=(stage / name).stat().st_size)
        (stage / provenance.name).write_text(json.dumps(result, indent=2) + '\n')
        render_full(argparse.Namespace(provenance=stage / provenance.name, output=stage / 'repeater_full_resolution.png',
                                       width=args.width, work=args.work))
        receipt_path = stage / 'repeater_full_resolution.json'
        receipt = json.loads(receipt_path.read_text())
        receipt['gds'] = str(source_gds)
        receipt_path.write_text(json.dumps(receipt, indent=2) + '\n')
        lines = ['# Current 1G single-die floorplan', '',
            f'All {len(placed)} interior candidates sit inside one **{die / 1000:.2f} × {die / 1000:.2f} mm die** with a shared IHP I/O padframe and sealring.',
            f'The core opening is {plan["core_opening_um"] / 1000:.2f} mm square. Minimum block separation and core margin are {plan["channel_um"]} µm and {plan["core_margin_um"]} µm.', '',
            '**Floorplan only: inter-block signal, clock and core-power connections remain unfinished.**',
            'The existing modules remain as-is, including both clock alternatives and standalone constituent circuits. This is not the production instance inventory or a fabrication-qualified ASIC.', '',
            '- [Single-die GDSII](repeater_development.gds.gz): open `RepeaterDevelopmentDie`.',
            '- [High-resolution image](repeater_full_resolution.png) and [render receipt](repeater_full_resolution.json).',
            '- [Layer views](repeater_layers.png).',
            '- [Geometry, placements and recorded checks](development_provenance.json).', '',
            '![Single-die floorplan](repeater_layers.png)', '',
            'The former small standalone padframe was replaced with real IHP pads, corner cells, supply fillers, decoupling and an enlarged PDK sealring. Padframe rail abutment is checked; no core power distribution is implied.',
            'Retained interior geometry hashes are unchanged; replacement candidates carry their own source hashes and recorded failures. The saved GDS was read back and checked for one top, the exact inventory, core containment, routing clearances and absence of padframe intrusion. Full-chip DRC, LVS, extracted timing and electrical qualification have not been run.', '',
            '| ID | Module | Recorded state |', '| --- | --- | --- |']
        for index, block in enumerate(records):
            lines.append(f'| {index + 1:02d} | [{block["title"]}]({block["name"]}.png) | {block["caption"]} |')
        lines += ['', '## Regeneration', '', 'From `analog/`, after preparing the IHP PDK:', '', '```sh',
            'PYTHONPATH=out/pdf-renderer pdk/IHPSG13G2/.cpphdl/venv/bin/python pdk/1G_repeater_IHPSG13G2/floorplan_development_layout.py',
            '```', '', 'The script uses this published GDS as input, preserves an immutable copy under `build/`, regenerates the padframe and replaces this single combined GDS and its images. The reviewed coordinates are in `pdk/1G_repeater_IHPSG13G2/development_floorplan.json`; changed block sizes or inventory must still pass its containment and clearance checks.', '',
            'Embedded IHP PDK geometry is distributed under [Apache-2.0](IHP-LICENSE.txt).', '']
        (stage / 'README.md').write_text('\n'.join(lines))
        if digest(source_gds) != source_hash or digest(provenance) != provenance_hash or digest(args.plan) != plan_hash:
            raise ValueError('floorplan inputs changed during publication')
        for name, expected in replacement_hashes.items():
            path = Path(name) if Path(name).is_absolute() else ROOT / name
            if digest(path) != expected:
                raise ValueError('replacement input changed during publication: ' + name)
        for name in (packed.name, provenance.name, 'repeater_layers.png', 'padframe_current.png',
                     'repeater_full_resolution.png', 'repeater_full_resolution.json', 'README.md',
                     *replacement_images):
            (stage / name).replace(provenance.parent / name)
    print(json.dumps(dict(status='floorplan_published', die_um=die, interior_blocks=len(placed),
                          electrically_integrated=False)), flush=True)


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--provenance', type=Path, default=ROOT / '1G/IHPSG13G2/results/development_provenance.json')
    parser.add_argument('--plan', type=Path, default=HERE / 'development_floorplan.json')
    parser.add_argument('--pdk', type=Path, default=ROOT / 'pdk/IHPSG13G2')
    parser.add_argument('--tools', type=Path, default=ROOT / 'out/repeater-toolchain')
    parser.add_argument('--work', type=Path, default=ROOT / 'build/layout-publication/single-die')
    parser.add_argument('--width', type=int, default=8000)
    run(parser.parse_args())
