#!/usr/bin/env python3
"""Check connected TX steering/cascode/sink geometry and extracted DC loss."""
import argparse
from collections import Counter
import json
import os
from pathlib import Path
import re
import sys
import tempfile
import xml.etree.ElementTree as ET

import klayout.db as db

SOURCE = Path(__file__).resolve()
ANALOG = SOURCE.parents[3]
sys.path[:0] = [str(SOURCE.parents[1]), str(ANALOG / 'pdk/1G_repeater_IHPSG13G2')]
from transmit_cell_layout import PORTS, append_transmit_cell
from extract_analog import digest, extract
from extracted_connections import check_capacitor_connections
from checks import check_lvs, check_drc_completion
from tool_process import configure_cleanup, execute
from mos_bank_dc import measure


def run(args):
    args.work.mkdir(parents=True, exist_ok=True)
    work = Path(tempfile.mkdtemp(prefix='run-', dir=args.work.resolve()))
    pdk = args.pdk.resolve()
    tech = pdk / 'ihp-sg13g2/libs.tech/klayout/tech'
    models = pdk / 'ihp-sg13g2/libs.tech/ngspice'
    paths = [SOURCE, SOURCE.with_name('mos_bank_dc.py'), args.ngspice.resolve(),
             args.tools.resolve() / 'root/usr/bin/klayout', models / '.spiceinit']
    paths += [ANALOG / 'pdk/1G_repeater_IHPSG13G2' / name for name in ('checks.py', 'tool_process.py')]
    paths += [SOURCE.parents[1] / name for name in ('transmit_cell_layout.py', 'mos_bank_layout.py',
              'mos_layout.py', 'extract_analog.py', 'extracted_connections.py', 'cascoded_core_transmit_driver.hpp')]
    pcell = pdk / 'ihp-sg13g2/libs.tech/klayout/python/sg13g2_pycell_lib'
    paths += [p for parent in (tech, models, pcell) for p in parent.rglob('*') if p.is_file()
              and p.suffix in ('.py', '.json', '.lvs', '.drc', '.spice', '.lib', '.osdi')]
    hashes = {str(p): digest(p) for p in paths}
    report = dict(status='running', work=str(work), source_sha256=hashes, cases={},
        scope='connected four-device current cell, local geometry and nominal extracted DC; excludes bias generation, translators, full TX, PVT, transient, fill and EM')
    env = dict(os.environ, PATH=str(args.tools.resolve() / 'bin') + os.pathsep + os.environ['PATH'],
        PDK_ROOT=str(pdk), PDK='ihp-sg13g2', SPICE_USERINIT_DIR=str(models), OMP_NUM_THREADS='1')

    def save():
        text = json.dumps(report, indent=2) + '\n'
        (work / 'checks.json').write_text(text)
        (args.work / 'checks.json').write_text(text)

    def command(target, label, argv):
        with (target / (label + '.log')).open('w') as log:
            return execute(list(map(str, argv)), cwd=target, env=env, stdout=log, stderr=-2,
                           timeout=300, memory_exclusive=False).returncode

    try:
        save()
        for history in (False, True):
            case = 'history' if history else 'main'
            target = work / case
            target.mkdir()
            result = report['cases'][case] = {}
            layout = db.Layout()
            layout.dbu = .001
            top, geometry = append_transmit_cell(layout, pdk, 'TransmitCell', history)
            result['geometry'] = geometry
            gds, schematic = target / 'cell.gds', target / 'cell.cir'
            layout.write(str(gds))
            schematic.write_text('* Independent four-transistor current-cell topology\n' + geometry['schematic'])
            command(target, 'lvs', [sys.executable, tech / 'lvs/run_lvs.py', '--layout', gds,
                '--netlist', schematic, '--topcell', top.name, '--run_mode', 'deep', '--run_dir', target / 'lvs'])
            result['lvs'] = check_lvs(target / 'lvs/cell.lvsdb', top.name, set(PORTS), 5)
            command(target, 'drc', [sys.executable, tech / 'drc/run_drc.py', '--path', gds,
                '--topcell', top.name, '--run_dir', target / 'drc', '--mp', 1, '--density_thr', 1, '--antenna'])
            drc = target / 'drc/cell_TransmitCell_full.lyrdb'
            result['drc_completion'] = check_drc_completion(drc, tech / 'drc')
            violations = Counter(item.findtext('category').strip("'")
                                 for item in ET.parse(drc).getroot().find('items'))
            minima = {'AFil.g', 'GFil.g', 'M1.j', 'M2.j', 'M3.j', 'M4.j', 'M5.j', 'TM1.c', 'TM2.c'}
            if set(violations) - minima:
                raise ValueError(case + ': geometry/max-density violation: ' + str(violations))
            result.update(local_drc_violations=0, unfilled_minimum_density_violations=dict(violations))
            # Physically sever the GP input spine immediately below its port.
            port = geometry['terminals']['GP']
            x, y = round(port['x_um'] / layout.dbu), round(port['y_um'] / layout.dbu)
            metal = layout.layer(134, 0)
            cut = db.Region(db.Box(x - 3000, y - 4000, x + 3000, y - 3000))
            remaining = db.Region(top.shapes(metal)) - cut
            top.shapes(metal).clear()
            top.shapes(metal).insert(remaining)
            broken = target / 'cut_gate.gds'
            layout.write(str(broken))
            command(target, 'cut-gate-lvs', [sys.executable, tech / 'lvs/run_lvs.py', '--layout', broken,
                '--netlist', schematic, '--topcell', top.name, '--run_mode', 'deep', '--run_dir', target / 'cut-lvs'])
            try:
                check_lvs(target / 'cut-lvs/cut_gate.lvsdb', top.name, set(PORTS), 5)
            except ValueError:
                result['cut_gate_rejected'] = True
            else:
                raise ValueError('LVS accepted disconnected steering input')
            # KLayout treats top-level text as ports, whereas Magic has a
            # separate non-port label layer. Add only internal node names to
            # an extraction view; assert every physical polygon is unchanged.
            annotated = db.Layout()
            annotated.read(str(gds))
            annotated_top = annotated.cell(top.name)
            before = {layer: db.Region(annotated_top.begin_shapes_rec(layer))
                      for layer in annotated.layer_indices()}
            for net, anchor in geometry['internal_anchors'].items():
                annotated_top.shapes(annotated.layer(anchor['layer'], 25)).insert(db.Text(net,
                    db.Trans(round(anchor['x_um'] / annotated.dbu), round(anchor['y_um'] / annotated.dbu))))
            for layer in annotated.layer_indices():
                if not (before.get(layer, db.Region()) ^ db.Region(annotated_top.begin_shapes_rec(layer))).is_empty():
                    raise ValueError('extraction annotation changed physical geometry')
            extraction_gds = target / 'named_extraction.gds'
            annotated.write(str(extraction_gds))
            result['extraction_annotations'] = dict(geometry_identical=True,
                internal_nonport_labels=geometry['internal_anchors'])
            extraction = extract(extraction_gds, 'TransmitCell', PORTS, pdk, args.magic,
                                 target / 'extraction', substrate='VSS')
            result['extraction'] = extraction
            for path, value in extraction['source_sha256'].items():
                if path in hashes and hashes[path] != value:
                    raise ValueError('extraction input changed between cell checks')
                hashes[path] = value
            if extraction['conserved']['models'] != {'sg13_hv_nmos': 210 if history else 550}:
                raise ValueError('extraction changed current-cell finger inventory')
            ext = (target / 'extraction/ExtractedAnalog.ext').read_text()
            ports = re.findall(r'^port "([^"]+)" \d+ [-\d ]+ (\w+)$', ext, re.M)
            if sorted(ports) != sorted((p, 'm7') for p in PORTS):
                raise ValueError('duplicate or wrong-layer current-cell external ports')
            result['connections'] = check_capacitor_connections(
                (target / 'extraction/conserved.spice').read_text(), PORTS)
            result['dc'] = {}
            for voltage in (1.3, 2.3, 3.3):
                for side in ('P', 'N'):
                    experiment = target / f'{side}-{voltage:g}'
                    experiment.mkdir()
                    active = ['PEX', 'OFF', 'G', '0'] if side == 'P' else ['OFF', 'PEX', '0', 'G']
                    deck = ('* Connected extracted current cell vs independent equal-finger PSP circuit\n'
                        f'.lib "{models / "models/cornerMOShv.lib"}" mos_tt\n' +
                        (target / 'extraction/conserved.spice').read_text() + '\n' +
                        f'VPEX PEX 0 {voltage}\nVREF REF 0 {voltage}\nVOFF OFF 0 {voltage}\n'
                        'VG G 0 3.3\nVCAS CAS 0 1.8\nVBIAS BIAS 0 0\n' +
                        'Xdevice ' + ' '.join(active + ['BIAS', 'CAS', '0']) + ' ExtractedAnalog\n')
                    widths = (200, 500, 150) if history else (400, 1500, 450)
                    for index, (width, nodes) in enumerate(((widths[0], 'REF G TAIL 0'),
                            (widths[0], 'OFF 0 TAIL 0'), (widths[1], 'TAIL CAS LOWER 0'),
                            (widths[2], 'LOWER BIAS 0 0'))):
                        for finger in range(width // 5):
                            deck += f'Xref{index}_{finger} {nodes} sg13_hv_nmos w=5u l=.45u\n'
                    deck += ('.temp 27\n.control\nset wr_singlescale\nset wr_vecnames\nset numdgt=17\n'
                        'dc VBIAS .6 1.2 .01\nlet extracted_current = -i(VPEX)\n'
                        'let reference_current = -i(VREF)\n'
                        'wrdata iv.dat v(BIAS) extracted_current reference_current\nquit\n.endc\n.end\n')
                    (experiment / 'bench.cir').write_text(deck)
                    if command(experiment, 'ngspice', [args.ngspice.resolve(), '-b', 'bench.cir']):
                        raise ValueError('connected-cell DC simulation failed')
                    measured = measure(experiment / 'iv.dat', .0025 if history else .0075)
                    result['dc'][experiment.name] = measured
                    if measured['maximum_relative_error'] > measured['relative_error_limit']:
                        raise ValueError(case + ' ' + experiment.name + ': extracted current differs by more than 5%')
            result['artifact_sha256'] = {str(p): digest(p) for p in target.rglob('*')
                if p.is_file() and p.suffix in ('.gds', '.cir', '.spice', '.dat', '.lvsdb', '.lyrdb')}
            save()
        if any(digest(p) != value for p, value in hashes.items()):
            raise ValueError('current-cell qualification inputs changed')
        report['status'] = 'pass'
        print('PASS main/history connected cells: local DRC/LVS, cut-input rejection, extracted DC')
    except BaseException as error:
        report.update(status='fail', error=str(error))
        raise
    finally:
        save()


if __name__ == '__main__':
    configure_cleanup()
    parser = argparse.ArgumentParser(description=__doc__)
    for name in ('pdk', 'tools', 'ngspice', 'magic', 'work'):
        parser.add_argument('--' + name, type=Path, required=True)
    run(parser.parse_args())
