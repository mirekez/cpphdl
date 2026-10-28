#!/usr/bin/env python3
"""Verify connected reference-clock geometry against its exported C++ circuit."""
import argparse
from collections import Counter
import hashlib
import json
import os
from pathlib import Path
import sys
import tempfile
import xml.etree.ElementTree as ET

SOURCE = Path(__file__).resolve()
ANALOG = SOURCE.parents[3]
sys.path[:0] = [str(SOURCE.parents[1]), str(ANALOG / 'pdk/1G_repeater_IHPSG13G2')]
from reference_clock_layout import create_reference_clock, circuit, PORTS
from lvs_view import create_flat_view
from checks import check_lvs, check_drc_completion
from tool_process import execute, configure_cleanup


def digest(path):
    with Path(path).open('rb') as stream:
        return hashlib.file_digest(stream, 'sha256').hexdigest()


def reference_lvs(path):
    import klayout.db as db
    # Flat simplification merges parallel fingers/plates and the common
    # substrate taps. Verify the complete normalized device inventory too.
    expected = dict(sg13_lv_nmos=112, sg13_lv_pmos=109, sg13_hv_nmos=5,
                    sg13_hv_pmos=4, cap_cmim=167, rppd=9, dpantenna=3, ntap1=116, ptap1=1)
    result = check_lvs(path, 'ReferenceClockGenerator', set(PORTS), sum(expected.values()))
    comparison = db.LayoutVsSchematic()
    comparison.read(str(path))
    top = next(p.first() for p in comparison.xref().each_circuit_pair()
               if p.first() is not None and p.first().name == 'ReferenceClockGenerator')
    inventory = Counter(d.device_class().name.lower() for d in top.each_device())
    if dict(inventory) != expected or list(top.each_subcircuit()):
        raise ValueError('reference-clock normalized physical inventory changed')
    result['normalized_device_inventory'] = dict(inventory)
    return result


def run(args):
    args.work.mkdir(parents=True, exist_ok=True)
    work = Path(tempfile.mkdtemp(prefix='run-', dir=args.work.resolve()))
    tech = args.pdk.resolve() / 'ihp-sg13g2/libs.tech/klayout'
    model = SOURCE.parents[1] / 'generated/OneGIHPReferenceClockGenerator.va'
    tracked = [SOURCE, model, args.exporter.resolve(), SOURCE.with_name('reference_clock_layout_export.cpp'),
        *[SOURCE.parents[1] / name for name in (
        'reference_clock_hierarchy.py',
        'reference_clock_layout.py', 'reference_clock_placement.py', 'reference_clock_process.py', 'analog_row_fold.py', 'antenna_layout.py', 'lvs_view.py',
        'reference_clock_row.py', 'processing_clock_gate_layout.py', 'processing_clock_gate_process.py',
        'oscillator_passive_layout.py', 'resistor_layout.py', 'capacitor_bank_layout.py',
        'core_driver_process.py', 'capacitor_layout.py', 'capacitor_series_layout.py', 'mos_layout.py')],
        ANALOG / 'pdk/1G_repeater_IHPSG13G2/checks.py', ANALOG / 'pdk/1G_repeater_IHPSG13G2/tool_process.py',
        ANALOG / 'pdk/1G_repeater_IHPSG13G2/fill_core.py',
        args.tools.resolve() / 'root/usr/bin/klayout', Path(sys.executable).resolve()]
    tracked += [p for p in tech.rglob('*') if p.is_file() and p.suffix in ('.py', '.json', '.lvs', '.drc', '.lym')]
    hashes = {str(p): digest(p) for p in tracked}
    report = dict(status='running', work=str(work), source_sha256=hashes, full_chip_drc=False,
        scope='filled 230-MOS PLL and clock translator, actual resistance, MIM and protection geometry; excludes extracted electrical performance and complete-chip integration')
    env = dict(os.environ, MALLOC_ARENA_MAX='2',
               PATH=str(args.tools.resolve() / 'bin') + os.pathsep + os.environ['PATH'])
    env.update(REPEATER_PDK=str(args.pdk.resolve()), REPEATER_TOP='ReferenceClockGenerator',
               REPEATER_SOURCE=str(ANALOG / 'pdk/1G_repeater_IHPSG13G2'))

    def save():
        text = json.dumps(report, indent=2) + '\n'
        (work / 'checks.json').write_text(text)
        (args.work / 'checks.json').write_text(text)

    def command(label, argv, timeout=1800):
        with (work / (label + '.log')).open('w') as log:
            # Fill introduces many floating islands. LVS needs more memory
            # than the unfilled macro and must serialize with digital routing.
            result = execute(['prlimit', '--as=3221225472', '--', *map(str, argv)], env=env, stdout=log, stderr=-2,
                timeout=timeout, memory_exclusive=True)
        report[label + '_exit_code'] = result.returncode

    save()
    try:
        with (work / 'export.log').open('w') as log:
            execute([args.exporter.resolve(), work], stdout=log, stderr=-2,
                    check=True, timeout=60, memory_exclusive=False)
        if digest(work / 'reference.va') != digest(model):
            raise ValueError('C++ hierarchy exporter differs from the physical circuit')
        hierarchy = json.loads((work / 'hierarchy.json').read_text())
        if set(hierarchy) != {'divider', 'reference_detector', 'feedback_detector'}:
            raise ValueError('reference-clock flip-flop hierarchy changed')
        hashes[str(work / 'hierarchy.json')] = digest(work / 'hierarchy.json')
        layout, top, info = create_reference_clock(args.pdk.resolve(), model, hierarchy)
        gds, netlist = work / 'reference.gds', work / 'reference.cir'
        unfilled = work / 'reference_unfilled.gds'
        layout.write(str(unfilled))
        netlist.write_text('* Exported C++ topology, explicit process dimensions and common p-substrate\n' + info['schematic'])
        report['geometry'] = info
        command('fill', [args.tools.resolve() / 'bin/klayout', '-zz', '-r',
            ANALOG / 'pdk/1G_repeater_IHPSG13G2/fill_core.py', '-rd', 'output_file=' + str(gds), unfilled])
        if report['fill_exit_code'] != 0 or not gds.is_file():
            raise ValueError('reference-clock PDK fill did not complete')
        view = work / 'lvs_input/reference.gds'
        report['lvs_view'] = create_flat_view(gds, view, 'ReferenceClockGenerator')
        command('lvs', [sys.executable, tech / 'tech/lvs/run_lvs.py', '--layout', view,
            '--netlist', netlist, '--topcell', 'ReferenceClockGenerator', '--run_mode', 'flat', '--run_dir', work / 'lvs'])
        command('drc', [sys.executable, tech / 'tech/drc/run_drc.py', '--path', gds,
            '--topcell', 'ReferenceClockGenerator', '--run_dir', work / 'drc', '--mp', 1, '--density_thr', 1, '--antenna'])
        lvs = work / 'lvs/reference.lvsdb'
        drc = work / 'drc/reference_ReferenceClockGenerator_full.lyrdb'
        # Retain independent verdicts even when one tool fails or is killed.
        failures = []
        try:
            report['lvs'] = reference_lvs(lvs)
            if report['lvs_exit_code'] != 0:
                raise ValueError('LVS process did not exit successfully')
        except Exception as error:
            report['lvs_error'] = str(error)
            failures.append('LVS: ' + str(error))
        try:
            report['drc_completion'] = check_drc_completion(drc, tech / 'tech/drc')
            violations = Counter(i.findtext('category').strip("'") for i in ET.parse(drc).getroot().find('items'))
            report['drc_violations'] = dict(violations)
            report['full_drc_pass'] = not violations and report['drc_exit_code'] == 0
            if not report['full_drc_pass']:
                raise ValueError('full DRC failed: ' + str(violations))
        except Exception as error:
            report['drc_error'] = str(error)
            failures.append('DRC: ' + str(error))
        if failures:
            raise ValueError('; '.join(failures))
        # Mutate the same filled GDS that passed, retaining all filler geometry.
        layout = type(layout)()
        layout.read(str(gds))
        top = layout.cell('ReferenceClockGenerator')
        # Break a real input branch without changing the source or pin labels.
        escape = next(p for p in info['escapes'] if p['kind'] == 'mos' and p['terminal'] == 'G' and p['net'] == 'CLEAR')
        x, y = (round(v / layout.dbu) for v in escape['location_um'])
        vias = [s for s in top.each_shape(layout.layer(49, 0)) if s.bbox().center().x == x and s.bbox().center().y == y]
        if len(vias) != 1:
            raise ValueError('ambiguous reference-clock damage location')
        vias[0].delete()
        damaged = work / 'open_gate.gds'
        layout.write(str(damaged))
        damaged_view = work / 'lvs_input/open_gate.gds'
        report['damaged_lvs_view'] = create_flat_view(damaged, damaged_view, 'ReferenceClockGenerator')
        command('open_gate_lvs', [sys.executable, tech / 'tech/lvs/run_lvs.py', '--layout', damaged_view,
            '--netlist', netlist, '--topcell', 'ReferenceClockGenerator', '--run_mode', 'flat', '--run_dir', work / 'open_gate_lvs'])
        try:
            reference_lvs(work / 'open_gate_lvs/open_gate.lvsdb')
        except ValueError:
            report['disconnected_clear_branch_rejected'] = True
        else:
            raise ValueError('LVS accepted a disconnected clear input branch')
        altered = work / 'unbound.va'
        altered.write_text(model.read_text().replace('0.002, 0.34999999999999998', '0.0021, 0.34999999999999998'))
        try:
            circuit(altered)
        except ValueError:
            report['unbound_geometry_rejected'] = True
        else:
            raise ValueError('unbound transistor parameters were accepted')
        if any(digest(p) != expected for p, expected in hashes.items()):
            raise ValueError('reference-clock geometry or PDK changed during verification')
        report.update(status='pass', artifact_sha256={str(p): digest(p) for p in
            (gds, unfilled, netlist, lvs, drc, damaged, view, damaged_view)})
        print('PASS filled reference-clock LVS/full DRC and rejected open input; full-chip verification remains required')
    except BaseException as error:
        report.update(status='fail', error=str(error))
        raise
    finally:
        save()


if __name__ == '__main__':
    configure_cleanup()
    parser = argparse.ArgumentParser(description=__doc__)
    for name in ('pdk', 'tools', 'work', 'exporter'):
        parser.add_argument('--' + name, type=Path, required=True)
    run(parser.parse_args())
