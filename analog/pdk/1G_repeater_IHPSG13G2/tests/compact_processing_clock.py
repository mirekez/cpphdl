#!/usr/bin/env python3
"""Verify compact clock topology; optionally qualify filled geometry with full DRC."""
import argparse
import json
import os
from pathlib import Path
import sys
import tempfile

import klayout.db as db

HERE = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(HERE))
sys.path.insert(0, str(HERE.parents[1] / 'out/pdf-renderer'))
from checks import check_drc, check_lvs
from compact_processing_clock import run as compact
from integrate_processing_clock import PORTS
from package_development_layout import ROOT, digest
from tool_process import configure_cleanup, execute


def open_clock(path, output, geometry):
    """Remove a RAW conductor while leaving all primitive devices intact."""
    layout = db.Layout()
    layout.read(str(path))
    top = layout.cell('ProcessingClockDelivery')
    rows = geometry['compaction']['row_buses']['RAW']
    if len(rows) > 1:
        x = round(rows[0]['x_min_um'] / layout.dbu)
        lo = round(min(row['y_um'] for row in rows) / layout.dbu)
        hi = round(max(row['y_um'] for row in rows) / layout.dbu)
        shapes = top.shapes(layout.layer(50, 0))
        matches = [shape for shape in shapes.each() if shape.is_box() and
                   shape.box.left < x < shape.box.right and
                   shape.box.bottom <= lo and shape.box.top >= hi]
    elif len(rows) == 1:
        row = rows[0]
        y = round(row['y_um'] / layout.dbu)
        lo, hi = [round(row[key] / layout.dbu) for key in ('x_min_um', 'x_max_um')]
        if hi - lo < round(1 / layout.dbu):
            raise ValueError('RAW negative control lacks distinct terminals')
        shapes = top.shapes(layout.layer(67, 0))
        matches = [shape for shape in shapes.each() if shape.is_box() and
                   shape.box.bottom < y < shape.box.top and
                   shape.box.left <= lo and shape.box.right >= hi]
    else:
        raise ValueError('negative control has no clock bus')
    if len(matches) != 1:
        raise ValueError('ambiguous RAW trunk in actual layout')
    matches[0].delete()
    layout.write(str(output))


def run(args):
    args.work.mkdir(parents=True, exist_ok=True)
    work = Path(tempfile.mkdtemp(prefix='run-', dir=args.work.resolve()))
    report = dict(status='running', work=str(work), full_chip_qualified=False,
                  electrical_qualified=False, drc_qualified=False, lvs_qualified=False)

    def save():
        content = json.dumps(report, indent=2) + '\n'
        (work / 'checks.json').write_text(content)
        (args.work / 'checks.json').write_text(content)

    env = dict(os.environ, PATH=str(args.tools.resolve() / 'bin') + os.pathsep + os.environ['PATH'],
               REPEATER_PDK=str(args.pdk.resolve()), REPEATER_TOP='ProcessingClockDelivery')
    tech = args.pdk.resolve() / 'ihp-sg13g2/libs.tech/klayout/tech'

    def command(stage, argv, timeout=240):
        report['stage'] = stage
        save()
        with (work / (stage + '.log')).open('w') as log:
            result = execute(list(map(str, argv)), env=env, stdout=log, stderr=-2,
                             timeout=timeout, memory_exclusive=True)
        report[stage + '_exit'] = result.returncode
        return result.returncode

    try:
        save()
        candidate = args.candidate.resolve() if args.candidate else work / 'candidate'
        if not args.candidate:
            compact(argparse.Namespace(work=candidate, provenance=args.provenance, width=600))
        generated = json.loads((candidate / 'compaction.json').read_text())
        report['source_sha256'] = dict(generated['source_sha256'])
        for name, expected in generated['artifact_sha256'].items():
            path = candidate / name
            if digest(path) != expected:
                raise ValueError('candidate differs from generation receipt: ' + name)
            report['source_sha256'][str(path)] = expected
        for path in (Path(__file__), HERE / 'checks.py', HERE / 'fill_core.py',
                     args.tools.resolve() / 'root/usr/bin/klayout'):
            report['source_sha256'][str(path)] = digest(path)
        for directory in ('lvs', 'drc', 'macros'):
            for path in (tech / directory).rglob('*'):
                if path.is_file() and path.suffix in ('.lvs', '.drc', '.lym', '.json', '.py'):
                    report['source_sha256'][str(path)] = digest(path)
        for path, expected in report['source_sha256'].items():
            if digest(Path(path)) != expected:
                raise ValueError('stale candidate source: ' + path)
        gds = candidate / 'compact.gds'
        if args.full_drc:
            gds = work / 'compact_filled.gds'
            if command('fill', [args.tools.resolve() / 'bin/klayout', '-zz', '-r', HERE / 'fill_core.py',
                       '-rd', 'output_file=' + str(gds), candidate / 'compact.gds'], 600):
                raise ValueError('PDK fill failed')
        opened = work / 'clock_open.gds'
        open_clock(gds, opened, generated['geometry'])
        for stage, layout_path, broken in (('lvs', gds, False), ('open_lvs', opened, True)):
            code = command(stage, [sys.executable, tech / 'lvs/run_lvs.py', '--layout', layout_path,
                '--netlist', candidate / 'compact.cir', '--topcell', 'ProcessingClockDelivery',
                '--run_mode', 'deep', '--run_dir', work / stage])
            database = work / stage / (layout_path.stem + '.lvsdb')
            if not database.is_file():
                raise ValueError('LVS produced no database')
            try:
                checked = check_lvs(database, 'ProcessingClockDelivery', PORTS, 398)
            except ValueError:
                if not broken:
                    raise
                comparison = db.LayoutVsSchematic()
                comparison.read(str(database))
                pairs = [pair for pair in comparison.xref().each_circuit_pair()
                         if pair.first() is not None and pair.first().name == 'ProcessingClockDelivery']
                log = (work / (stage + '.log')).read_text()
                if (len(pairs) != 1 or pairs[0].second() is None or
                        "Netlists don't match" not in log or 'LVS Total Run time' not in log):
                    raise ValueError('negative LVS comparison did not complete')
                report['opened_clock_trunk_rejected'] = True
            else:
                if broken or code or checked['instantiated_devices'] != 398:
                    raise ValueError('unexpected LVS result or device inventory')
                report.update(lvs=checked, lvs_qualified=True)
        if args.full_drc:
            code = command('drc', [sys.executable, tech / 'drc/run_drc.py', '--path', gds,
                '--topcell', 'ProcessingClockDelivery', '--run_dir', work / 'drc',
                '--mp', '1', '--density_thr', '1', '--antenna'], 600)
            report['drc'] = check_drc(work / 'drc' /
                (gds.stem + '_ProcessingClockDelivery_full.lyrdb'), tech / 'drc')
            if code:
                raise ValueError('DRC process failed')
            report['drc_qualified'] = True
        for path, expected in report['source_sha256'].items():
            if digest(Path(path)) != expected:
                raise ValueError('verification input changed: ' + path)
        report.update(status='pass', stage='complete', candidate=str(candidate), gds=str(gds),
            primitive_geometry_preserved=generated['primitive_geometry_preserved'],
            area_um2=generated['area_um2'], area_reduction_percent=generated['area_reduction_percent'])
        print('PASS compact clock: 398 devices, 22 ports, opened clock trunk rejected', flush=True)
    except BaseException as error:
        report.update(status='fail', error=str(error))
        raise
    finally:
        report['artifact_sha256'] = {str(path): digest(path) for path in work.rglob('*')
            if path.is_file() and path.suffix in ('.gds', '.lvsdb', '.lyrdb', '.log')}
        save()


if __name__ == '__main__':
    configure_cleanup()
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--work', type=Path, required=True)
    parser.add_argument('--candidate', type=Path)
    parser.add_argument('--full-drc', action='store_true')
    parser.add_argument('--provenance', type=Path, default=ROOT / '1G/IHPSG13G2/results/development_provenance.json')
    parser.add_argument('--pdk', type=Path, default=ROOT / 'pdk/IHPSG13G2')
    parser.add_argument('--tools', type=Path, default=ROOT / 'out/repeater-toolchain')
    run(parser.parse_args())
