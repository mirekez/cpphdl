#!/usr/bin/env python3
"""Check real MIM plate dimensions, contacts, area and strict two-terminal LVS."""
import argparse
from collections import Counter
import hashlib
import json
import os
from pathlib import Path
import sys
import tempfile
import xml.etree.ElementTree as ET

import klayout.db as db

SOURCE = Path(__file__).resolve()
ANALOG = SOURCE.parents[3]
sys.path[:0] = [str(SOURCE.parents[1]), str(ANALOG / 'pdk/1G_repeater_IHPSG13G2')]
from capacitor_layout import append_mim
from capacitor_series_layout import append_mim_series
from checks import check_lvs, check_drc_completion
from tool_process import configure_cleanup, execute


def digest(path):
    return hashlib.sha256(Path(path).read_bytes()).hexdigest()


def run(args):
    root = args.work.resolve()
    root.mkdir(parents=True, exist_ok=True)
    work = Path(tempfile.mkdtemp(prefix='run-', dir=root))
    technology = args.pdk.resolve() / 'ihp-sg13g2/libs.tech/klayout/tech'
    environment = dict(os.environ, PATH=str(args.tools.resolve() / 'bin') + os.pathsep + os.environ['PATH'])
    tracked = [SOURCE, SOURCE.parents[1] / 'mos_layout.py', SOURCE.parents[1] / 'capacitor_layout.py',
               SOURCE.parents[1] / 'capacitor_series_layout.py',
               ANALOG / 'pdk/1G_repeater_IHPSG13G2/checks.py',
               ANALOG / 'pdk/1G_repeater_IHPSG13G2/tool_process.py',
               technology / 'lvs/run_lvs.py', technology / 'drc/run_drc.py',
               args.tools.resolve() / 'root/usr/bin/klayout', Path(sys.executable).resolve()]
    tracked += [p for p in technology.rglob('*') if p.is_file() and p.suffix in ('.lvs', '.drc', '.json')]
    pcell = args.pdk.resolve() / 'ihp-sg13g2/libs.tech/klayout/python/sg13g2_pycell_lib'
    tracked += [p for p in pcell.rglob('*') if p.is_file() and p.suffix in ('.py', '.json')]
    hashes = {str(p): digest(p) for p in tracked}
    report = dict(status='running', cases={}, full_chip_drc=False, source_sha256=hashes,
        scope='strict-port MIM LVS and complete local DRC tables; unfilled or over-dense isolated global windows remain failing')
    report['run_directory'] = str(work)

    def save():
        payload = json.dumps(report, indent=2) + '\n'
        for destination in (work / 'checks.json', root / 'checks.json'):
            destination.write_text(payload)

    def command(case, label, argv):
        with (case / (label + '.log')).open('w') as log:
            result = execute(list(map(str, argv)), env=environment, stdout=log, stderr=-2, timeout=300, memory_exclusive=False)
        return result.returncode

    try:
        cases = (('small', 100e-15, False), ('quarter_pf', .25e-12, False),
                                       ('half_pf', .5e-12, False), ('three_quarter_pf', .75e-12, False),
                                       ('one_pf', 1e-12, False), ('one_half_pf', 1.5e-12, False),
                                       ('two_pf', 2e-12, False), ('three_pf', 3e-12, False),
                                       ('four_pf', 4e-12, False), ('open_top', 4e-12, True))
        if args.small:
            cases = (('two_ff_series', 2e-15, False), ('four_ff', 4e-15, False),
                     ('eight_ff', 8e-15, False), ('open_junction', 2e-15, True))
        for label, storage, damage in cases:
            case = work / label
            case.mkdir(exist_ok=True)
            layout = db.Layout()
            layout.dbu = .001
            series = storage == 2e-15
            make = append_mim_series if series else append_mim
            top, info = make(layout, args.pdk, 'Storage', storage)
            if damage:
                # Removing physical plate contacts must fail even though the
                # intended schematic and both terminal labels remain present.
                if series:
                    top.shapes(layout.layer(125, 0)).clear()
                else:
                    device = layout.cell(next(top.each_inst()).cell_index)
                    device.shapes(layout.layer(129, 0)).clear()
            gds, netlist = case / 'device.gds', case / 'device.cir'
            layout.write(str(gds))
            netlist.write_text('* Independent intended MIM dimensions and plate orientation\n' + info['schematic'])
            command(case, 'lvs', [sys.executable, technology / 'lvs/run_lvs.py', '--layout', gds,
                '--netlist', netlist, '--topcell', 'Storage', '--run_mode', 'deep', '--run_dir', case / 'lvs'])
            lvs = case / 'lvs/device.lvsdb'
            if damage:
                try:
                    check_lvs(lvs, 'Storage', {'PLUS', 'MINUS'}, 1)
                except ValueError:
                    report['cases'][label] = dict(expected_open_top_rejected=True)
                else:
                    raise ValueError('LVS accepted an unconnected physical top plate')
            else:
                result = check_lvs(lvs, 'Storage', {'PLUS', 'MINUS'}, 2 if series else 1)
                command(case, 'drc', [sys.executable, technology / 'drc/run_drc.py', '--path', gds,
                    '--topcell', 'Storage', '--run_dir', case / 'drc', '--mp', 1, '--density_thr', 1, '--antenna'])
                drc = case / 'drc/device_Storage_full.lyrdb'
                completion = check_drc_completion(drc, technology / 'drc')
                drc_root = ET.parse(drc).getroot()
                categories = {c.findtext('name') for c in drc_root.find('categories').iter('category')}
                if len(categories) < 800 or not {'Ant.a', 'Ant.g', 'Pin.a'}.issubset(categories):
                    raise ValueError('incomplete local DRC/antenna/pin rule inventory')
                violations = Counter(i.findtext('category').strip("'") for i in drc_root.find('items'))
                density_rules = {'AFil.g', 'GFil.g', 'M1.j', 'M2.j', 'M3.j', 'M4.j', 'M5.j', 'M5.k', 'TM1.c', 'TM1.d', 'TM2.c'}
                if set(violations) - density_rules:
                    raise ValueError('local MIM geometry, antenna or pin violation: ' + str(violations))
                report['cases'][label] = dict(geometry=info, lvs=result, drc_completion=completion,
                    local_drc_violations=0, full_drc_pass=not violations,
                    global_density_violations=dict(violations),
                    artifact_sha256={str(p): digest(p) for p in (gds, netlist, lvs, drc)})
            save()
            print(label + ': pass', flush=True)
        for storage in (0, -1e-12, float('nan'), float('inf'), 2e-15, 8e-12, 9e-12):
            layout = db.Layout()
            layout.dbu = .001
            try:
                append_mim(layout, args.pdk, 'Invalid', storage)
            except ValueError:
                continue
            raise ValueError('invalid storage was accepted')
        if not args.small:
            a = report['cases']['two_pf']['geometry']['bounding_area_um2']
            b = report['cases']['four_pf']['geometry']['bounding_area_um2']
            report['additional_sampling_capacitor_footprint_um2'] = dict(per_slice=b - a,
                per_lane=16 * (b - a), two_port_eight_lane_repeater=128 * (b - a))
            candidate = report['cases']['one_half_pf']['geometry']['bounding_area_um2']
            preceding = report['cases']['three_pf']['geometry']['bounding_area_um2']
            if not candidate < preceding:
                raise ValueError('smaller sampling storage did not reduce its physical footprint')
            report['programmable_receiver_storage_saving_um2'] = dict(
                candidate_storage_F=1.5e-12, preceding_storage_F=3e-12,
                candidate_footprint_per_slice=candidate, preceding_footprint_per_slice=preceding,
                per_slice=preceding - candidate, per_lane=16 * (preceding - candidate),
                two_port_eight_lane_repeater=128 * (preceding - candidate),
                scope='sum of capacitor bounding footprints; excludes placement/routing and does not establish die savings')
            footprint = lambda name: report['cases'][name]['geometry']['bounding_area_um2']
            preceding_compensation = 2 * footprint('one_pf') + sum(footprint(name) for name in ('two_pf', 'three_pf', 'four_pf'))
            candidate_compensation = 2 * footprint('quarter_pf') + sum(footprint(name) for name in ('half_pf', 'three_quarter_pf', 'one_pf'))
            report['wideband_compensation_saving_um2'] = dict(
                preceding_requested_total_F=11e-12, candidate_requested_total_F=2.75e-12,
                preceding_footprint_per_lane=preceding_compensation, candidate_footprint_per_lane=candidate_compensation,
                per_lane=preceding_compensation - candidate_compensation,
                two_port_eight_lane_repeater=8 * (preceding_compensation - candidate_compensation),
                scope='all five physical compensation capacitors, including unselected branches; summed bounding footprint, not die savings')
        if any(digest(p) != value for p, value in hashes.items()):
            raise ValueError('MIM layout/checker/PDK sources changed during qualification')
        report['status'] = 'pass'
    except BaseException as error:
        report.update(status='fail', error=str(error))
        raise
    finally:
        save()


if __name__ == '__main__':
    configure_cleanup()
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--pdk', type=Path, required=True)
    parser.add_argument('--tools', type=Path, required=True)
    parser.add_argument('--work', type=Path, required=True)
    parser.add_argument('--small', action='store_true', help='check clock-gate storage and the physical series junction')
    run(parser.parse_args())
