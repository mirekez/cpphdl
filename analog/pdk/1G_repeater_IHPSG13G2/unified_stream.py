#!/usr/bin/env python3
"""Export real cell geometry from the common digital placement checkpoint."""
import argparse
import json
from pathlib import Path

import klayout.db as db

from unified_synthesis import MACRO, digest


def run(args):
    proof = json.loads(args.proof.read_text())
    if proof.get('status') != 'placed_unrouted':
        raise ValueError('completed common placement required')
    hashes = {**proof['source_sha256'], **proof['artifact_sha256']}
    if any(digest(path) != value for path, value in hashes.items()):
        raise ValueError('placement checkpoint is stale')
    work = args.proof.resolve().parent
    pdk = args.pdk.resolve() / 'ihp-sg13g2'
    technology = pdk / 'libs.tech/klayout/tech'
    cells = pdk / 'libs.ref/sg13g2_stdcell'
    memories = pdk / 'libs.ref/sg13g2_sram'
    tech = db.Technology()
    tech.load(str(technology / 'sg13g2.lyt'))
    options = tech.load_layout_options
    config = options.lefdef_config
    config.lef_files = [str(work / 'route_tech.lef'), str(cells / 'lef/sg13g2_stdcell.lef'),
                        str(memories / f'lef/{MACRO}.lef')]
    config.map_file = str(technology / 'sg13g2.map')
    config.produce_cell_outlines = False
    config.produce_placement_blockages = False
    config.produce_regions = False
    config.produce_obstructions = False
    config.produce_blockages = False
    options.lefdef_config = config
    layout = db.Layout()
    layout.read(str(work / 'placed.def'), options)
    top = layout.cell('RepeaterDigital')
    if top is None:
        raise ValueError('missing digital placement top')
    population = list(top.each_inst())
    if sum(i.cell.name == MACRO for i in population) != 8:
        raise ValueError('placement stream lost SRAMs')
    if any(i.cell.name != MACRO and not i.cell.name.startswith('sg13g2_') for i in population):
        raise ValueError('unexpected logical block in physical stream')
    instances = len(population)
    del population
    for cell in layout.each_cell():
        if cell.cell_index() != top.cell_index() and not cell.name.startswith('VIA_'):
            cell.clear()
    libraries = [cells / 'gds/sg13g2_stdcell.gds', memories / f'gds/{MACRO}.gds']
    for path in [Path(__file__), args.proof.resolve(), technology / 'sg13g2.lyt',
                 technology / 'sg13g2.map']:
        hashes[str(path)] = digest(path)
    library_empty = set()
    for path in libraries:
        hashes[str(path)] = digest(path)
        original = db.Layout()
        original.read(str(path))
        library_empty.update(cell.name for cell in original.each_cell() if cell.is_empty())
        del original
        layout.read(str(path))
    # Some SRAM library helper cells are empty in the supplied GDS itself.
    # Preserve those exactly, while rejecting absent or empty placed masters.
    if any(instance.cell.is_empty() for instance in top.each_inst()):
        raise ValueError('placed master has no physical geometry')
    result = db.Layout()
    result.dbu = layout.dbu
    final = result.create_cell(top.name)
    final.copy_tree(top)
    empty = [cell.name for cell in result.each_cell()
             if cell.is_empty() and cell.name not in library_empty]
    if empty or final.bbox().empty():
        raise ValueError('missing physical geometry: ' + ', '.join(empty))
    args.output.mkdir(parents=True, exist_ok=False)
    path = args.output / 'digital_placement.gds.gz'
    result.write(str(path))
    # Re-read the exported geometry so the receipt describes the actual file.
    saved = db.Layout()
    saved.read(str(path))
    restored = saved.cell('RepeaterDigital')
    if (len(list(saved.top_cells())) != 1 or restored is None or
            len(list(restored.each_inst())) != instances or restored.bbox() != final.bbox()):
        raise ValueError('physical stream round trip changed placement')
    artifacts = {path.name: digest(path)}
    if args.image_width:
        from package_receive_results import LAYERS, render
        if not 1000 <= args.image_width <= 8000:
            raise ValueError('image width must be between 1000 and 8000 pixels')
        box = restored.dbbox()
        bounds = [box.left, box.bottom, box.right, box.top]
        image, _ = render(path, args.output, 'drawing', list(LAYERS), bounds,
                          args.image_width, args.image_width)
        png = args.output / 'digital_placement.png'
        image.save(png)
        artifacts[png.name] = digest(png)
    if any(digest(p) != value for p, value in hashes.items()):
        raise ValueError('stream inputs changed')
    report = dict(status='placed_unrouted', full_chip_qualified=False,
                  scope='Common digital placement with real standard-cell and SRAM geometry; '
                        'analog devices, pads, power, CTS and routing are not included.',
                  top=final.name, instances=instances, sram_macros=8, fixed_logic_modules=0,
                  empty_helpers_from_source_libraries=sorted(library_empty),
                  placement=str(args.proof.resolve()), source_sha256=hashes,
                  artifact_sha256=artifacts)
    (args.output / 'checks.json').write_text(json.dumps(report, indent=2) + '\n')
    print(json.dumps({key: report[key] for key in ('status', 'instances', 'fixed_logic_modules')}))


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    for name in ('proof', 'pdk', 'output'):
        parser.add_argument('--' + name, type=Path, required=True)
    parser.add_argument('--image-width', type=int, default=4000)
    run(parser.parse_args())
