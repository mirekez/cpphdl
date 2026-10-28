#!/usr/bin/env python3
"""Compare the same two-wire GDS in Magic and an independent IHP field geometry."""
import argparse
import json
import math
import os
from pathlib import Path
import re
import sys
import tempfile

SOURCE = Path(__file__).resolve()
ANALOG = SOURCE.parents[2]
sys.path.insert(0, str(ANALOG / 'pdk/1G_repeater_IHPSG13G2'))
from extract_analog import digest, number
from field_matrix import read_matrix, coupling, grounded_pair
from input_charge import verify_files
from tool_process import execute, configure_cleanup


def run(args):
    solver = json.loads(args.solver_preparation.read_text())
    geometry = json.loads(args.geometry_preparation.read_text())
    if (solver.get('status') != 'prepared' or solver.get('version') != '6.0.9'
            or geometry.get('status') != 'prepared' or geometry.get('version') != '0.6.12'):
        raise ValueError('requires pinned FasterCap and KLayout-PEX preparation')
    if not 0 < args.tolerance <= .01:
        raise ValueError('field convergence tolerance must be positive and at most one percent')
    hashes = {}
    for preparation in (solver, geometry):
        verify_files(preparation['artifact_sha256'])
        hashes.update(preparation['artifact_sha256'])
    disk_qualified = False
    if args.disk_proof:
        proof = json.loads(args.disk_proof.read_text())
        if (proof.get('status') != 'pass' or proof.get('disk_mode_qualified') is not True
                or proof.get('preparation_sha256') != digest(args.solver_preparation)
                or solver.get('local_corrections') != ['per-block-matrix-offset']):
            raise ValueError('disk proof does not qualify this exact solver preparation')
        verify_files(proof['source_sha256'])
        for case in proof['cases'].values():
            verify_files(case['artifact_sha256'])
        hashes.update(proof['source_sha256'])
        hashes[str(args.disk_proof.resolve())] = digest(args.disk_proof)
        disk_qualified = True
    if not 2 <= args.ooc_threshold <= 100:
        raise ValueError('out-of-core threshold must be between 2 and 100')
    if len(set(args.gaps)) != len(args.gaps):
        raise ValueError('duplicate wire gap')
    if (any(not math.isfinite(gap) or not 0 < gap < 8 for gap in args.gaps) or
            any(not math.isfinite(value) or value <= 0 for value in (args.length, args.width))):
        raise ValueError('requires positive wire dimensions and gaps within the 8 um extraction halo')
    if len({f'{gap:.12g}' for gap in args.gaps}) != len(args.gaps):
        raise ValueError('wire gaps produce duplicate fixture names')
    layer = (8, 10, 30, 50, 67, 126, 134)[args.metal - 1]
    cell_name = 'CoupledM' + str(args.metal)
    technology = args.pdk.resolve() / 'ihp-sg13g2/libs.tech/magic'
    files = [SOURCE, SOURCE.with_name('field_matrix.py'), SOURCE.with_name('extract_analog.py'),
        SOURCE.with_name('input_charge.py'), ANALOG / 'pdk/1G_repeater_IHPSG13G2/tool_process.py',
        args.magic.resolve(), args.klayout.resolve(), args.solver_preparation.resolve(),
        args.geometry_preparation.resolve(), *sorted(technology.glob('*'))]
    hashes.update({str(p): digest(p) for p in files if p.is_file()})
    args.work.mkdir(parents=True, exist_ok=True)
    work = Path(tempfile.mkdtemp(prefix='run-', dir=args.work.resolve()))
    report = dict(status='running', work=str(work), source_sha256=hashes, cases={},
        geometry=dict(length_um=args.length, width_um=args.width, layer=[layer, 0], coordinate_unit_m=1e-6),
        field_tolerance=args.tolerance, disk_mode_qualified=disk_qualified,
        scope='two-wire comparison using bundled KLayout-PEX IHP stack; excludes calibrated coefficients, '
              'domain convergence, FEOL/poly, transistor devices and complete-chip qualification')

    def save():
        text = json.dumps(report, indent=2) + '\n'
        (work / 'checks.json').write_text(text)
        (args.work / 'checks.json').write_text(text)

    save()
    try:
        for gap in sorted(args.gaps):
            case = work / f'gap-{gap:.12g}'
            case.mkdir()
            # The generator runs in the pinned KLayout Python environment.
            script = case / 'geometry.py'
            script.write_text('''import klayout.db as db
layout = db.Layout()
layout.dbu = .001
cell = layout.create_cell('CELL_NAME')
for name, bottom in [('A', 0.), ('B', WIDTH + GAP)]:
    box = db.DBox(0., bottom, LENGTH, bottom + WIDTH)
    for datatype in (0, 2):
        cell.shapes(layout.layer(LAYER, datatype)).insert(box)
    cell.shapes(layout.layer(LAYER, 25)).insert(db.DText(name, db.DTrans(LENGTH / 2, bottom + WIDTH / 2)))
layout.write('wires.gds')
'''.replace('GAP', repr(gap)).replace('WIDTH', repr(args.width))
                .replace('LENGTH', repr(args.length)).replace('LAYER', str(layer))
                .replace('CELL_NAME', cell_name))
            environment = dict(os.environ, OMP_NUM_THREADS='1',
                KPEX_KLAYOUT_EXE=str(args.klayout.resolve()), KPEX_FASTERCAP_EXE=solver['executable'])
            with (case / 'geometry.log').open('w') as log:
                execute([geometry['python'], str(script)], cwd=case, env=environment,
                    stdout=log, stderr=-2, check=True, timeout=30, memory_exclusive=False)
            with (case / 'field.log').open('w') as log:
                execute(['prlimit', '--as=1073741824', '--', geometry['executable'], 'extract',
                    '--pdk', 'ihp-sg13g2', '--gds', str(case / 'wires.gds'), '--cell', cell_name,
                    '--fastercap', '--blackbox', 'true', '--threads', '1', '--k-void', '4.1',
                    '--tolerance', str(args.tolerance), '--galerkin', '--geo-check', 'true',
                    '--ooc', str(args.ooc_threshold),
                    '--out-dir', str(case / 'field')], cwd=case, env=environment,
                    stdout=log, stderr=-2, check=True, timeout=1800, memory_exclusive=False)
            field = case / ('field/wires__' + cell_name)
            # FasterCap interprets coordinate values as metres. KPEX emits
            # micrometres; capacitance scales linearly with physical length.
            matrix = read_matrix((field / (cell_name + '_FasterCap_Output.txt')).read_text(),
                                 1e-6, args.tolerance, allow_disk=disk_qualified)
            extracted = coupling(matrix, 'A', 'B')
            magic = case / 'magic'
            magic.mkdir()
            commands = ['drc off', 'gds read {' + str(case / 'wires.gds') + '}',
                'load ' + cell_name, 'select top cell', 'expand', 'extract all',
                'ext2spice format ngspice', 'ext2spice scale off', 'ext2spice hierarchy off',
                'ext2spice subcircuit top on', 'ext2spice merge none', 'ext2spice cthresh 0',
                'ext2spice -o wires.spice']
            (magic / 'extract.tcl').write_text('if {[catch {\n' + '\n'.join(commands) +
                '\n} message]} { puts stderr $message; exit 1 }\nquit -noprompt\n')
            with (magic / 'extract.log').open('w') as log:
                execute([str(args.magic.resolve()), '-dnull', '-noconsole', '-rcfile',
                    str(technology / 'ihp-sg13g2.magicrc'), 'extract.tcl'], cwd=magic,
                    env=environment, stdout=log, stderr=-2, check=True, timeout=60, memory_exclusive=False)
            log = (magic / 'extract.log').read_text()
            if re.search(r'\b(?:error|unrecognized|unknown|failed)\b', log, re.I):
                raise ValueError('Magic extraction diagnostic requires inspection')
            netlist = (magic / 'wires.spice').read_text()
            records = re.findall(r'^C\S+\s+(\S+)\s+(\S+)\s+(\S+)\s*$', netlist, re.M)
            matched = [number(value) for a, b, value in records if {a, b} == {'A', 'B'}]
            if len(matched) != 1 or extracted <= 0:
                raise ValueError('missing or duplicated two-wire coupling')
            field_pair = grounded_pair(matrix, 'A', 'B')
            loads = {}
            for signal in ('A', 'B'):
                grounded = [number(value) for a, b, value in records if {a, b} == {signal, 'sub'}]
                if len(grounded) != 1 or grounded[0] <= 0:
                    raise ValueError('missing or ambiguous two-wire substrate capacitance')
                magic_loads = dict(same_direction_F=grounded[0],
                    quiet_neighbor_F=grounded[0] + matched[0],
                    opposite_direction_F=grounded[0] + 2 * matched[0])
                loads[signal] = {mode: dict(magic_F=value,
                    field_F=field_pair['signals'][signal][mode],
                    relative_difference=value / field_pair['signals'][signal][mode] - 1)
                    for mode, value in magic_loads.items()}
            report['cases'][str(gap)] = dict(gap_um=gap, field=matrix,
                field_coupling_F=extracted, magic_coupling_F=matched[0],
                magic_relative_difference=matched[0] / extracted - 1,
                field_grounded_pair=field_pair, switching_loads=loads,
                artifact_sha256={str(p): digest(p) for p in sorted(case.rglob('*')) if p.is_file()})
            save()
        results = list(report['cases'].values())
        for key in ('field_coupling_F', 'magic_coupling_F'):
            values = [row[key] for row in results]
            if not all(a > b for a, b in zip(values, values[1:])):
                raise ValueError('coupling did not decrease with greater wire separation')
        verify_files(hashes)
        report['status'] = 'characterized'
        print('Completed two-wire field/Magic comparison; this does not qualify extraction accuracy')
    except BaseException as error:
        report.update(status='fail', error=str(error))
        raise
    finally:
        save()


if __name__ == '__main__':
    configure_cleanup()
    parser = argparse.ArgumentParser(description=__doc__)
    for name in ('solver-preparation', 'geometry-preparation', 'magic', 'klayout', 'pdk', 'work'):
        parser.add_argument('--' + name, type=Path, required=True)
    parser.add_argument('--tolerance', type=float, default=.01)
    parser.add_argument('--disk-proof', type=Path, help='passing oracle for this exact corrected solver preparation')
    parser.add_argument('--ooc-threshold', type=float, default=2.)
    parser.add_argument('--metal', type=int, choices=range(1, 8), default=1)
    parser.add_argument('--width', type=float, default=.5, help='wire width in micrometres')
    parser.add_argument('--length', type=float, default=20., help='facing wire length in micrometres')
    parser.add_argument('--gaps', type=float, nargs='+', default=(.2, .4, .8))
    run(parser.parse_args())
