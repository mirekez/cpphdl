#!/usr/bin/env python3
"""Verify connected processing-oscillator geometry against its exported C++ circuit."""
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
from processing_clock_oscillator_layout import create_oscillator, circuit, PORTS
from checks import check_lvs, check_drc_completion
from tool_process import execute, configure_cleanup


def digest(path):
    with Path(path).open('rb') as stream:
        return hashlib.file_digest(stream, 'sha256').hexdigest()


def run(args):
    args.work.mkdir(parents=True, exist_ok=True)
    work = Path(tempfile.mkdtemp(prefix='run-', dir=args.work.resolve()))
    tech = args.pdk.resolve() / 'ihp-sg13g2/libs.tech/klayout'
    model = SOURCE.parents[1] / 'generated/OneGIHPProcessingClockOscillator.va'
    tracked = [SOURCE, model, *[SOURCE.parents[1] / name for name in (
        'processing_clock_oscillator_layout.py', 'processing_clock_process.py',
        'analog_row_layout.py', 'processing_clock_gate_layout.py', 'processing_clock_gate_process.py',
        'oscillator_passive_layout.py', 'resistor_layout.py', 'antenna_layout.py',
        'core_driver_process.py', 'capacitor_layout.py', 'capacitor_series_layout.py', 'mos_layout.py')],
        ANALOG / 'pdk/1G_repeater_IHPSG13G2/checks.py', ANALOG / 'pdk/1G_repeater_IHPSG13G2/tool_process.py',
        args.tools.resolve() / 'root/usr/bin/klayout', Path(sys.executable).resolve()]
    tracked += [p for p in tech.rglob('*') if p.is_file() and p.suffix in ('.py', '.json', '.lvs', '.drc')]
    hashes = {str(p): digest(p) for p in tracked}
    report = dict(status='running', work=str(work), source_sha256=hashes, full_chip_drc=False,
        scope='connected 51-MOS oscillator, actual resistance and MIM geometry and substrate contacts; excludes fill and extracted electrical performance')
    env = dict(os.environ, PATH=str(args.tools.resolve() / 'bin') + os.pathsep + os.environ['PATH'])

    def save():
        text = json.dumps(report, indent=2) + '\n'
        (work / 'checks.json').write_text(text)
        (args.work / 'checks.json').write_text(text)

    def command(label, argv):
        with (work / (label + '.log')).open('w') as log:
            result = execute(list(map(str, argv)), env=env, stdout=log, stderr=-2,
                timeout=600, memory_exclusive=False)
        report[label + '_exit_code'] = result.returncode

    save()
    try:
        layout, top, info = create_oscillator(args.pdk.resolve(), model)
        gds, netlist = work / 'oscillator.gds', work / 'oscillator.cir'
        layout.write(str(gds))
        netlist.write_text('* Exported C++ topology, explicit process dimensions and common p-substrate\n' + info['schematic'])
        report['geometry'] = info
        command('lvs', [sys.executable, tech / 'tech/lvs/run_lvs.py', '--layout', gds,
            '--netlist', netlist, '--topcell', 'ProcessingClockOscillator', '--run_mode', 'deep', '--run_dir', work / 'lvs'])
        command('drc', [sys.executable, tech / 'tech/drc/run_drc.py', '--path', gds,
            '--topcell', 'ProcessingClockOscillator', '--run_dir', work / 'drc', '--mp', 1, '--density_thr', 1, '--antenna'])
        lvs = work / 'lvs/oscillator.lvsdb'
        drc = work / 'drc/oscillator_ProcessingClockOscillator_full.lyrdb'
        report['lvs'] = check_lvs(lvs, 'ProcessingClockOscillator', set(PORTS), 294)
        report['drc_completion'] = check_drc_completion(drc, tech / 'tech/drc')
        violations = Counter(i.findtext('category').strip("'") for i in ET.parse(drc).getroot().find('items'))
        density = {'AFil.g', 'GFil.g', 'M1.j', 'M2.j', 'M3.j', 'M4.j', 'M5.j', 'M5.k', 'TM1.c', 'TM1.d', 'TM2.c'}
        report['drc_violations'] = dict(violations)
        report['full_drc_pass'] = not violations
        if set(violations) - density:
            raise ValueError('oscillator local geometry/antenna/pin violations: ' + str(violations))
        # Break a real input branch without changing the source or pin labels.
        escape = next(p for p in info['escapes'] if p['kind'] == 'mos' and p['terminal'] == 'G' and p['net'] == 'CLEAR')
        x, y = (round(v / layout.dbu) for v in escape['location_um'])
        vias = [s for s in top.each_shape(layout.layer(49, 0)) if s.bbox().center().x == x and s.bbox().center().y == y]
        if len(vias) != 1:
            raise ValueError('ambiguous oscillator damage location')
        vias[0].delete()
        damaged = work / 'open_gate.gds'
        layout.write(str(damaged))
        command('open_gate_lvs', [sys.executable, tech / 'tech/lvs/run_lvs.py', '--layout', damaged,
            '--netlist', netlist, '--topcell', 'ProcessingClockOscillator', '--run_mode', 'deep', '--run_dir', work / 'open_gate_lvs'])
        try:
            check_lvs(work / 'open_gate_lvs/open_gate.lvsdb', 'ProcessingClockOscillator', set(PORTS), 294)
        except ValueError:
            report['disconnected_clear_branch_rejected'] = True
        else:
            raise ValueError('LVS accepted a disconnected clear input branch')
        altered = work / 'unbound.va'
        altered.write_text(model.read_text().replace('0.0040000000000000001', '0.0041'))
        try:
            circuit(altered)
        except ValueError:
            report['unbound_geometry_rejected'] = True
        else:
            raise ValueError('unbound transistor parameters were accepted')
        if any(digest(p) != expected for p, expected in hashes.items()):
            raise ValueError('oscillator geometry or PDK changed during verification')
        report.update(status='pass', artifact_sha256={str(p): digest(p) for p in (gds, netlist, lvs, drc, damaged)})
        print('PASS connected oscillator LVS/local DRC and rejected open input; full-chip verification remains required')
    except BaseException as error:
        report.update(status='fail', error=str(error))
        raise
    finally:
        save()


if __name__ == '__main__':
    configure_cleanup()
    parser = argparse.ArgumentParser(description=__doc__)
    for name in ('pdk', 'tools', 'work'):
        parser.add_argument('--' + name, type=Path, required=True)
    run(parser.parse_args())
