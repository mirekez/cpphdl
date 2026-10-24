"""Compare hierarchical fill to the original PDK masks on repeated real cells."""
import argparse
import hashlib
import json
import os
from pathlib import Path
import subprocess
import tempfile
import pya

SOURCE = Path(__file__).resolve()


def instances(cell, allowed=None):
    return sorted((i.cell.name, str(i.cplx_trans), str(i.a), str(i.b), i.na, i.nb)
                  for i in cell.each_inst() if allowed is None or i.cell.name in allowed)


def run(args):
    args.work.mkdir(parents=True, exist_ok=True)
    work = Path(tempfile.mkdtemp(prefix='fill-', dir=args.work))
    layout = pya.Layout()
    layout.read(str(args.pdk / 'ihp-sg13g2/libs.ref/sg13g2_stdcell/gds/sg13g2_stdcell.gds'))
    top = layout.create_cell('FillGeometry')
    for offset, name in enumerate(('sg13g2_inv_1', 'sg13g2_nand2_1', 'sg13g2_decap_8')):
        cell = layout.cell(name)
        if cell is None:
            raise ValueError('missing reference master: ' + name)
        top.insert(pya.CellInstArray(cell.cell_index(), pya.Trans(offset % 2, bool(offset % 2), 15000 + 20000 * offset, 15000),
                                    pya.Vector(0, 6000), pya.Vector(10000, 0), 8, 2))
    top.shapes(layout.layer(189, 0)).insert(pya.Box(0, 0, 100000, 100000))
    keep = {top.cell_index(), *top.called_cells()}
    layout.delete_cells([cell.cell_index() for cell in layout.each_cell() if cell.cell_index() not in keep])
    original = work / 'input.gds'
    layout.write(str(original))
    # Compare the serialized input consumed by both tool invocations. GDS
    # normalizes array transforms when the in-memory fixture is written.
    layout = pya.Layout()
    layout.read(str(original))
    reference = work / 'reference.py'
    reference.write_text('''import os
import pya
from pathlib import Path
technology = Path(os.environ['REPEATER_PDK']) / 'ihp-sg13g2/libs.tech/klayout/tech'
macro = pya.Macro(str(technology / 'macros/sg13g2_filler_ActGatP.lym'))
macro.text = macro.text.replace('EdgeSeal.holes', 'source.extent')
macro.run()
pya.CellView.active().layout().write(output_file)
''')
    environment = dict(os.environ, REPEATER_PDK=str(args.pdk), REPEATER_TOP='FillGeometry')
    outputs = []
    for label, script in (('reference', reference), ('hierarchical', SOURCE.parents[1] / 'fill_core.py')):
        output = work / (label + '.gds')
        with (work / (label + '.log')).open('w') as log:
            subprocess.run([str(args.klayout), '-zz', '-r', str(script), '-rd', 'output_file=' + str(output), str(original)],
                           env=environment, stdout=log, stderr=-2, check=True, timeout=90)
        result = pya.Layout()
        result.read(str(output))
        outputs.append(result)
    reference, actual = outputs
    checks = {}
    for number in (1, 5):
        old = pya.Region(reference.top_cell().begin_shapes_rec(reference.layer(number, 22)))
        new = pya.Region(actual.top_cell().begin_shapes_rec(actual.layer(number, 22)))
        checks['fill_' + str(number)] = not old.is_empty() and (old ^ new).is_empty()
    names = {cell.name for cell in layout.each_cell()}
    for cell in layout.each_cell():
        current = actual.cell(cell.name)
        if current is None:
            raise ValueError('fill removed a functional cell')
        checks[cell.name + '_instances'] = instances(cell) == instances(current, names)
        for info in layout.layer_infos():
            checks[cell.name + '_' + str(info)] = sorted(s.to_s() for s in cell.shapes(layout.layer(info)).each()) == \
                sorted(s.to_s() for s in current.shapes(actual.layer(info)).each())
    sources = [SOURCE, SOURCE.parents[1] / 'fill_core.py'] + list((args.pdk / 'ihp-sg13g2/libs.tech/klayout/tech/macros').glob('sg13g2_filler_*.lym'))
    report = dict(status='pass' if all(checks.values()) else 'fail', checks=checks, work=str(work),
                  input_sha256={str(p): hashlib.sha256(p.read_bytes()).hexdigest() for p in sources})
    (args.work / 'checks.json').write_text(json.dumps(report, indent=2) + '\n')
    if report['status'] != 'pass':
        raise ValueError('hierarchical fill changed reference masks or functional hierarchy')
    print('PASS exact Active/Poly fill masks and original cells, shapes and connections')


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--pdk', type=Path, required=True)
    parser.add_argument('--klayout', type=Path, required=True)
    parser.add_argument('--work', type=Path, required=True)
    args = parser.parse_args()
    args.pdk, args.klayout, args.work = (p.resolve() for p in (args.pdk, args.klayout, args.work))
    run(args)
