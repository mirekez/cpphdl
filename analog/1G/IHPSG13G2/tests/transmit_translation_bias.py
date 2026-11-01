#!/usr/bin/env python3
"""Check the exported shared replica, physical wiring and startup at three corners."""
import argparse
from collections import Counter
import json
import math
import os
from pathlib import Path
import re
import sys
import tempfile
import xml.etree.ElementTree as ET

SOURCE = Path(__file__).resolve()
ANALOG = SOURCE.parents[3]
sys.path[:0] = [str(SOURCE.parents[1]), str(ANALOG / '1G/tests'),
               str(ANALOG / 'VerilogA'), str(ANALOG / 'pdk/1G_repeater_IHPSG13G2')]
from transmit_translation_bias_layout import PORTS, create_translation_bias
from named_node_extraction import extract_named
from resistor_body_extraction import adapt
from extract_analog import digest
from hardware import source_dependencies
from checks import check_lvs, check_drc_completion
from tool_process import configure_cleanup, execute
from trace_storage import archive_trace


def reference(geometry):
    lines = ['.subckt ReferenceBias ' + ' '.join(PORTS)]
    for index, part in enumerate(geometry['components']):
        info, nodes = part['geometry'], part['connections']
        if part['kind'] == 'mos':
            for finger in range(info['fingers']):
                lines.append(f'Xmos{index}_{finger} ' + ' '.join(nodes[p] for p in ('D', 'G', 'S', 'B')) +
                    f' {info["model"]} w={info["finger_width_um"]}u l={info["length_um"]}u')
        elif part['kind'] == 'resistor':
            lines.append(f'Xres{index} ' + ' '.join(nodes[p] for p in ('PLUS', 'MINUS', 'SUB')) +
                f' rppd w={info["width_um"]}u l={info["length_um"]}u')
        else:
            unit = info['unit']
            for cap in range(info['capacitors']):
                lines.append(f'Xstorage{cap} BIAS VSS cap_cmim w={unit["width_um"]}u l={unit["length_um"]}u ic=0')
    return '\n'.join(lines + ['.ends ReferenceBias', ''])


def measure(path):
    with path.open() as stream:
        next(stream)
        rows = [[float(v) for v in line.split()] for line in stream if line.strip()]
    if (len(rows) < 10000 or any(len(r) != 11 or not all(math.isfinite(v) for v in r) for r in rows)
            or not 0 <= rows[0][0] <= 200e-12 or abs(rows[-1][0] - 2e-6) > 1e-14
            or any(not 0 < b[0] - a[0] <= 200.1e-12 for a, b in zip(rows, rows[1:]))):
        raise ValueError('incomplete replica startup trace')
    steady = [r for r in rows if r[0] >= 1.6e-6]
    result = {}
    for name, bias, io, logic, current_io, current_logic in (
            ('physical', 1, 3, 5, 7, 9), ('schematic', 2, 4, 6, 8, 10)):
        values = [r[bias] for r in steady]
        checks = dict(headroom=1.2 < min(values) <= max(values) < 2.3,
            settled=max(values) - min(values) <= .001,
            supply=all(r[io] >= 3.05 and r[logic] >= 1.08 for r in steady),
            bounded_startup=all(-.1 < r[bias] < 3.45 for r in rows),
            supply_current=all(0 < -r[current_io] < .005 and 0 < -r[current_logic] < .005 for r in steady))
        result[name] = dict(checks=checks, bias_min_V=min(values), bias_max_V=max(values))
        if not all(checks.values()):
            raise ValueError(name + ' replica electrical limits failed: ' + str(checks))
    error = max(abs(r[1] / r[2] - 1) for r in steady)
    if error > .05:
        raise ValueError('replica wiring exceeds the 5% settled bias budget')
    result['comparison'] = dict(maximum_settled_relative_error=error, limit=.05)
    return result


def run(args):
    pdk, tools = args.pdk.resolve(), args.tools.resolve()
    tech = pdk / 'ihp-sg13g2/libs.tech/klayout/tech'
    models = pdk / 'ihp-sg13g2/libs.tech/ngspice'
    args.work.mkdir(parents=True, exist_ok=True)
    work = Path(tempfile.mkdtemp(prefix='run-', dir=args.work.resolve()))
    paths = [SOURCE, args.exporter.resolve(), args.ngspice.resolve(), tools / 'root/usr/bin/klayout', models / '.spiceinit']
    paths += [SOURCE.parents[1] / name for name in ('transmit_translation_bias_layout.py', 'transmit_translation_bias_routes.py',
        'reference_clock_row.py', 'processing_clock_gate_layout.py', 'mos_layout.py', 'resistor_layout.py',
        'capacitor_bank_layout.py', 'capacitor_layout.py', 'core_driver_process.py', 'named_node_extraction.py',
        'extract_analog.py', 'resistor_body_extraction.py', 'resistor_body_model.py', 'extracted_connections.py')]
    paths += [ANALOG / 'pdk/1G_repeater_IHPSG13G2' / name for name in ('tool_process.py', 'checks.py')]
    paths += [ANALOG / 'VerilogA/trace_storage.py', ANALOG / '1G/tests/hardware.py']
    pcell = tech.parent / 'python/sg13g2_pycell_lib'
    paths += [p for directory in (tech, pcell, models) for p in directory.rglob('*') if p.is_file()
              and p.suffix in ('.py', '.json', '.lvs', '.drc', '.spice', '.lib', '.osdi')]
    hashes = {str(p): digest(p) for p in paths}
    hashes.update(source_dependencies([SOURCE.with_name('transmit_translation_bias_export.cpp')],
        ANALOG.parent, args.cxx, ['-std=c++20', '-I' + str(ANALOG), '-I' + str(ANALOG / 'include')]))
    report = dict(status='running', work=str(work), source_sha256=hashes,
        scope='shared replica with declared 50 pF storage; local DRC/LVS and three MOS/temperature startup corners; '
              'nominal passive models, unloaded BIAS; connected translators, fill, supply/passive corners and chip qualification separate')
    env = dict(os.environ, PATH=str(tools / 'bin') + os.pathsep + os.environ['PATH'],
        PDK_ROOT=str(pdk), PDK='ihp-sg13g2', SPICE_USERINIT_DIR=str(models), OMP_NUM_THREADS='1')

    def save():
        text = json.dumps(report, indent=2) + '\n'
        (work / 'checks.json').write_text(text)
        (args.work / 'checks.json').write_text(text)

    def command(label, argv):
        report['stage'] = label
        save()
        with (work / (label + '.log')).open('w') as log:
            return execute(list(map(str, argv)), cwd=work, env=env, stdout=log, stderr=-2,
                timeout=600, memory_exclusive=False).returncode

    try:
        if command('export', [args.exporter.resolve(), work / 'export']):
            raise ValueError('replica export failed')
        source = work / 'export/translation_bias.va'
        hashes[str(source)] = digest(source)
        layout, top, geometry = create_translation_bias(pdk, source)
        report['geometry'] = geometry
        gds, schematic = work / 'replica.gds', work / 'replica.cir'
        layout.write(str(gds))
        schematic.write_text(geometry['schematic'])
        command('lvs', [sys.executable, tech / 'lvs/run_lvs.py', '--layout', gds, '--netlist', schematic,
            '--topcell', top.name, '--run_mode', 'deep', '--run_dir', work / 'lvs'])
        report['lvs'] = check_lvs(work / 'lvs/replica.lvsdb', top.name, set(PORTS), 5)
        command('drc', [sys.executable, tech / 'drc/run_drc.py', '--path', gds, '--topcell', top.name,
            '--run_dir', work / 'drc', '--mp', 1, '--density_thr', 1, '--antenna'])
        drc = work / 'drc/replica_TransmitTranslationBias_full.lyrdb'
        report['drc_completion'] = check_drc_completion(drc, tech / 'drc')
        violations = Counter(item.findtext('category').strip("'") for item in ET.parse(drc).getroot().find('items'))
        minima = {'AFil.g', 'GFil.g', 'M1.j', 'M2.j', 'M3.j', 'M4.j', 'M5.j', 'TM1.c', 'TM2.c'}
        if set(violations) - minima:
            raise ValueError('replica geometry/maximum-density violations: ' + str(violations))
        report.update(local_drc_violations=0, retained_minimum_density_violations=dict(violations))
        victim = [i for i in top.each_inst() if i.cell.name == 'ReplicaMOS']
        if len(victim) != 1:
            raise ValueError('ambiguous replica transistor')
        victim[0].delete()
        broken = work / 'missing_replica.gds'
        layout.write(str(broken))
        command('missing-lvs', [sys.executable, tech / 'lvs/run_lvs.py', '--layout', broken, '--netlist', schematic,
            '--topcell', top.name, '--run_mode', 'deep', '--run_dir', work / 'missing-lvs'])
        try:
            check_lvs(work / 'missing-lvs/missing_replica.lvsdb', top.name, set(PORTS), 5)
        except ValueError:
            report['missing_replica_rejected'] = True
        else:
            raise ValueError('LVS accepted a missing replica transistor')
        report['stage'] = 'extraction'
        save()
        directory = work / 'extraction'
        extraction = extract_named(gds, top.name, PORTS, pdk, args.magic, directory, substrate='VSS')
        report['extraction'] = extraction
        hashes.update(extraction['source_sha256'])
        if extraction['inventory']['models'] != {'sg13_hv_nmos': 20, 'rppd': 3, 'cap_cmim': 10}:
            raise ValueError('replica extracted inventory changed')
        physical, report['resistor_body_partition'] = adapt((directory / 'named/capacitance.spice').read_text(),
            (directory / 'conserved.spice').read_text(), (directory / 'named/ExtractedAnalog.ext').read_text(), PORTS)
        physical = re.sub(r'^(X[^\n]+\bcap_cmim\b[^\n]*)$', r'\1 ic=0', physical, flags=re.M)
        report['electrical'] = {}
        for corner, temperature in (('mos_tt', 27), ('mos_ss', 125), ('mos_ff', -40)):
            text = (f'* Physical and independent replica startup at {corner}/{temperature}C\n'
                f'.lib "{models / "models/cornerMOShv.lib"}" {corner}\n'
                f'.lib "{models / "models/cornerRES.lib"}" res_typ\n'
                f'.lib "{models / "models/cornerCAP.lib"}" cap_typ\n.temp {temperature}\n' + physical + reference(geometry))
            for suffix, circuit_name in (('P', 'ExtractedAnalog'), ('R', 'ReferenceBias')):
                text += (f'VS{suffix} SP{suffix} 0 3.3\nVL{suffix} SL{suffix} 0 1.2\n'
                    f'RS{suffix} SP{suffix} VDD{suffix} 10\nRL{suffix} SL{suffix} L{suffix} 10\n'
                    f'CS{suffix} VDD{suffix} 0 2p ic=0\nCL{suffix} L{suffix} 0 .5p ic=0\n'
                    f'Xdut{suffix} VDD{suffix} 0 L{suffix} B{suffix} {circuit_name}\n')
            text += ('.options klu method=gear maxord=2\n.control\nset num_threads=1\nset wr_singlescale\n'
                'set wr_vecnames\nset numdgt=17\ntran 100p 2u 0 200p uic\n'
                f'wrdata {corner}.dat v(BP) v(BR) v(VDDP) v(VDDR) v(LP) v(LR) i(VSP) i(VSR) i(VLP) i(VLR)\n'
                'quit\n.endc\n.end\n')
            deck = work / (corner + '.cir')
            deck.write_text(text)
            if command(corner, ['prlimit', '--as=1073741824', '--', args.ngspice.resolve(), '-b', deck]):
                raise ValueError('replica startup simulation failed')
            trace = work / (corner + '.dat')
            try:
                report['electrical'][corner] = measure(trace)
            finally:
                archive, storage = archive_trace(trace)
                report.setdefault('trace_storage', {})[corner] = storage
                report.setdefault('artifact_sha256', {}).update({str(p): digest(p) for p in (deck, archive)})
        if any(digest(p) != h for p, h in hashes.items()):
            raise ValueError('replica inputs changed during verification')
        report['artifact_sha256'].update({str(p): digest(p) for p in (gds, schematic, source, drc,
            work / 'lvs/replica.lvsdb', directory / 'conserved.spice')})
        report.update(status='pass', stage='complete')
        print('PASS shared translation replica local DRC/LVS, missing-device rejection and three startup corners')
    except BaseException as error:
        report.update(status='fail', error=str(error))
        raise
    finally:
        save()


if __name__ == '__main__':
    configure_cleanup()
    parser = argparse.ArgumentParser(description=__doc__)
    for name in ('pdk', 'tools', 'magic', 'ngspice', 'exporter', 'work'):
        parser.add_argument('--' + name, type=Path, required=True)
    parser.add_argument('--cxx', default='/usr/bin/c++')
    run(parser.parse_args())
