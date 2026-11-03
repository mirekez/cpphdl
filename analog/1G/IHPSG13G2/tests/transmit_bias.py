#!/usr/bin/env python3
"""Check TX trim/cascode reference geometry and all 64 extracted DC codes."""
import argparse
from collections import Counter
import json
import math
import os
from pathlib import Path
import sys
import tempfile
import xml.etree.ElementTree as ET

import klayout.db as db

SOURCE = Path(__file__).resolve()
ANALOG = SOURCE.parents[3]
sys.path[:0] = [str(SOURCE.parents[1]), str(ANALOG / 'pdk/1G_repeater_IHPSG13G2')]
from transmit_bias_layout import PORTS, create_transmit_bias
from resistor_body_extraction import adapt
from extracted_connections import check_capacitor_connections
from extract_analog import digest, extract
from checks import check_lvs, check_drc_completion
from tool_process import configure_cleanup, execute


def reference(geometry):
    lines = ['.subckt ReferenceBias ' + ' '.join(PORTS)]
    for index, part in enumerate(geometry['components']):
        info, nodes = part['geometry'], part['connections']
        if part['kind'] == 'mos':
            for finger in range(info['fingers']):
                lines.append(f'Xmos{index}_{finger} ' + ' '.join(nodes[p] for p in ('D', 'G', 'S', 'B')) +
                    f' {info["model"]} w={info["finger_width_um"]}u l={info["length_um"]}u')
        else:
            lines.append(f'Xres{index} ' + ' '.join(nodes[p] for p in ('PLUS', 'MINUS', 'SUB')) +
                f' rppd w={info["width_um"]}u l={info["length_um"]}u')
    return '\n'.join(lines + ['.ends ReferenceBias', ''])


def measure(path):
    rows = [[float(value) for value in line.split()] for line in path.read_text().splitlines()[1:]]
    if (len(rows) != 64 or any(len(row) != 7 or not all(math.isfinite(x) for x in row) for row in rows)
            or any(abs(row[0] - code) > 1e-9 for code, row in enumerate(rows))):
        raise ValueError('incomplete or invalid 64-code bias sweep')
    result = {}
    for name, bias, cascode, current in (('physical', 1, 2, 3), ('schematic', 4, 5, 6)):
        checks = dict(bias_range=all(.6 < row[bias] < 1.2 for row in rows),
            cascode_range=all(abs(row[cascode] / 1.8 - 1) <= .05 for row in rows),
            positive_supply_current=all(0 < -row[current] < .005 for row in rows),
            monotonic_trim=all(b[bias] > a[bias] + 1e-6 for a, b in zip(rows, rows[1:])))
        result[name] = dict(checks=checks, bias_min_V=min(row[bias] for row in rows),
            bias_max_V=max(row[bias] for row in rows), cascode_min_V=min(row[cascode] for row in rows),
            cascode_max_V=max(row[cascode] for row in rows), supply_max_A=max(-row[current] for row in rows))
        if not all(checks.values()):
            raise ValueError(name + ' bias circuit failed independent limits: ' + str(checks))
    errors = {name: max(abs(row[a] / row[b] - 1) for row in rows)
              for name, a, b in (('bias', 1, 4), ('cascode', 2, 5), ('supply_current', 3, 6))}
    result['comparison'] = dict(maximum_relative_errors=errors, relative_error_limit=.05)
    if any(value > .05 for value in errors.values()):
        raise ValueError('bias wiring exceeds the 5% local comparison budget: ' + str(errors))
    return result


def run(args):
    pdk = args.pdk.resolve()
    tech = pdk / 'ihp-sg13g2/libs.tech/klayout/tech'
    models = pdk / 'ihp-sg13g2/libs.tech/ngspice'
    args.work.mkdir(parents=True, exist_ok=True)
    work = Path(tempfile.mkdtemp(prefix='run-', dir=args.work.resolve()))
    paths = [SOURCE, args.ngspice.resolve(), args.tools.resolve() / 'root/usr/bin/klayout', models / '.spiceinit']
    paths += [SOURCE.parents[1] / name for name in ('transmit_bias_layout.py', 'reference_clock_row.py',
        'processing_clock_gate_layout.py', 'resistor_layout.py', 'mos_layout.py',
        'extract_analog.py', 'resistor_body_extraction.py', 'resistor_body_model.py', 'extracted_connections.py',
        'biased_driver.hpp', 'cascoded_core_transmit_driver.hpp', 'core_driver_process.py')]
    paths += [ANALOG / 'pdk/1G_repeater_IHPSG13G2' / name for name in ('checks.py', 'tool_process.py')]
    pcell = pdk / 'ihp-sg13g2/libs.tech/klayout/python/sg13g2_pycell_lib'
    paths += [p for parent in (tech, models, pcell) for p in parent.rglob('*')
              if p.is_file() and p.suffix in ('.py', '.json', '.lvs', '.drc', '.spice', '.lib', '.osdi')]
    hashes = {str(p): digest(p) for p in paths}
    report = dict(status='running', work=str(work), source_sha256=hashes,
        scope='connected trim/cascode bias, local DRC/LVS and 64-code TT extracted DC; excludes storage, translators, full TX, PVT, fill, calibrated fields and EM')
    env = dict(os.environ, PATH=str(args.tools.resolve() / 'bin') + os.pathsep + os.environ['PATH'],
        PDK_ROOT=str(pdk), PDK='ihp-sg13g2', SPICE_USERINIT_DIR=str(models), OMP_NUM_THREADS='1')

    def save():
        text = json.dumps(report, indent=2) + '\n'
        (work / 'checks.json').write_text(text)
        (args.work / 'checks.json').write_text(text)

    def command(label, argv):
        with (work / (label + '.log')).open('w') as log:
            return execute(list(map(str, argv)), cwd=work, env=env, stdout=log, stderr=-2,
                           timeout=300, memory_exclusive=False).returncode

    try:
        save()
        layout, top, geometry = create_transmit_bias(pdk)
        report['geometry'] = geometry
        gds, schematic = work / 'bias.gds', work / 'bias.cir'
        layout.write(str(gds))
        schematic.write_text('* Trim reference and cascode divider topology\n' + geometry['schematic'])
        command('lvs', [sys.executable, tech / 'lvs/run_lvs.py', '--layout', gds, '--netlist', schematic,
            '--topcell', top.name, '--run_mode', 'deep', '--run_dir', work / 'lvs'])
        report['lvs'] = check_lvs(work / 'lvs/bias.lvsdb', top.name, set(PORTS), 16)
        command('drc', [sys.executable, tech / 'drc/run_drc.py', '--path', gds, '--topcell', top.name,
            '--run_dir', work / 'drc', '--mp', 1, '--density_thr', 1, '--antenna'])
        drc = work / 'drc/bias_TransmitBias_full.lyrdb'
        report['drc_completion'] = check_drc_completion(drc, tech / 'drc')
        violations = Counter(item.findtext('category').strip("'") for item in ET.parse(drc).getroot().find('items'))
        minima = {'AFil.g', 'GFil.g', 'M1.j', 'M2.j', 'M3.j', 'M4.j', 'M5.j', 'TM1.c', 'TM2.c'}
        if set(violations) - minima:
            raise ValueError('bias geometry/maximum-density violations: ' + str(violations))
        report.update(local_drc_violations=0, retained_minimum_density_violations=dict(violations))
        # Remove the bit-zero switch itself, retaining its wires and resistor.
        victim = [inst for inst in top.each_inst() if inst.cell.name == 'BiasSwitch0']
        if len(victim) != 1:
            raise ValueError('ambiguous trim-switch fault insertion')
        victim[0].delete()
        broken = work / 'missing_switch.gds'
        layout.write(str(broken))
        command('missing-switch-lvs', [sys.executable, tech / 'lvs/run_lvs.py', '--layout', broken,
            '--netlist', schematic, '--topcell', top.name, '--run_mode', 'deep', '--run_dir', work / 'missing-lvs'])
        try:
            check_lvs(work / 'missing-lvs/missing_switch.lvsdb', top.name, set(PORTS), 16)
        except ValueError:
            report['missing_switch_rejected'] = True
        else:
            raise ValueError('LVS accepted missing trim switch')
        extraction = extract(gds, top.name, PORTS, pdk, args.magic, work / 'extraction', substrate='VSS')
        report['extraction'] = extraction
        hashes.update(extraction['source_sha256'])
        if extraction['conserved']['models'] != {'sg13_hv_nmos': 10, 'sg13_hv_pmos': 60, 'rppd': 9}:
            raise ValueError('bias extraction changed device inventory')
        directory = work / 'extraction'
        physical, report['resistor_body_partition'] = adapt((directory / 'capacitance.spice').read_text(),
            (directory / 'conserved.spice').read_text(), (directory / 'ExtractedAnalog.ext').read_text(), PORTS)
        report['connections'] = check_capacitor_connections(physical, PORTS)
        text = ('* 64 trim codes, actual extracted and independent schematic bias circuits\n'
            f'.lib "{models / "models/cornerMOShv.lib"}" mos_tt\n'
            f'.lib "{models / "models/cornerRES.lib"}" res_typ\n.temp 27\n' + physical + reference(geometry) +
            'VPEX VDDP 0 3.3\nVREF VDDR 0 3.3\nVCODE CODE 0 0\n')
        for bit in range(6):
            text += (f'Btrim{bit} TRIM_N{bit} 0 V=3.3*(1-floor(v(CODE)/{1 << bit})'
                     f'+2*floor(v(CODE)/{2 << bit}))\n')
        controls = ' '.join(PORTS[4:])
        text += ('Xphysical VDDP 0 BIASP CASP ' + controls + ' ExtractedAnalog\n'
            'Xreference VDDR 0 BIASR CASR ' + controls + ' ReferenceBias\n'
            '.control\nset num_threads=1\nset wr_singlescale\nset wr_vecnames\nset numdgt=17\n'
            'dc VCODE 0 63 1\nwrdata bias.dat v(BIASP) v(CASP) i(VPEX) v(BIASR) v(CASR) i(VREF)\n'
            'quit\n.endc\n.end\n')
        (work / 'bench.cir').write_text(text)
        if command('ngspice', [args.ngspice.resolve(), '-b', 'bench.cir']):
            raise ValueError('transmitter bias DC simulation failed')
        report['electrical'] = measure(work / 'bias.dat')
        if any(digest(p) != value for p, value in hashes.items()):
            raise ValueError('transmitter bias inputs changed')
        report['artifact_sha256'] = {str(p): digest(p) for p in (gds, schematic, drc,
            work / 'lvs/bias.lvsdb', work / 'bench.cir', work / 'bias.dat')}
        report['status'] = 'pass'
        print('PASS connected TX bias: local DRC/LVS, missing-switch rejection and 64-code extracted DC')
    except BaseException as error:
        report.update(status='fail', error=str(error))
        raise
    finally:
        save()


if __name__ == '__main__':
    configure_cleanup()
    parser = argparse.ArgumentParser(description=__doc__)
    for name in ('pdk', 'tools', 'magic', 'ngspice', 'work'):
        parser.add_argument('--' + name, type=Path, required=True)
    run(parser.parse_args())
