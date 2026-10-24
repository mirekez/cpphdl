#!/usr/bin/env python3
"""Qualify the C++ inverter, its PSP dimensions and its actual routed layout."""
import argparse
import bisect
from collections import Counter
import hashlib
import itertools
import json
import math
import os
from pathlib import Path
import re
import sys
import xml.etree.ElementTree as ET

SOURCE = Path(__file__).resolve()
ANALOG = SOURCE.parents[3]
sys.path[:0] = [str(SOURCE.parents[1]), str(ANALOG / '1G/tests'), str(ANALOG / 'VerilogA'),
               str(ANALOG / 'pdk/1G_repeater_IHPSG13G2')]
from inverter_layout import create_inverter, inverter_source, process_experiment
from hardware import configure_cleanup, execute, source_dependencies
from compare import read_cpp, read_spice
from checks import check_lvs, check_drc_completion


def digest(path):
    return hashlib.sha256(Path(path).read_bytes()).hexdigest()


def at(rows, time, column):
    times = [r[0] for r in rows]
    index = min(max(1, bisect.bisect_left(times, time)), len(rows) - 1)
    a, b = rows[index - 1], rows[index]
    return a[column] + (b[column] - a[column]) * (time - a[0]) / (b[0] - a[0])


def contract(rows, supply):
    if (not rows or not 0 <= rows[0][0] <= .051e-12 or abs(rows[-1][0] - 24e-9) > 1e-15
            or any(len(row) != 4 or not all(math.isfinite(v) for v in row) for row in rows)
            or any(not 0 < b[0] - a[0] <= 5.1e-12 for a, b in zip(rows, rows[1:]))):
        raise ValueError('incomplete, nonfinite or undersampled inverter waveform')
    low, high = [], []
    for index in range(5):
        high_time, low_time = (2.8 + 4 * index) * 1e-9, (4.8 + 4 * index) * 1e-9
        if (abs(at(rows, high_time, 1) - supply) > .01 * supply
                or abs(at(rows, low_time, 1)) > .01 * supply):
            raise ValueError('inverter input delivery failed')
        low.append(at(rows, high_time, 2))
        high.append(at(rows, low_time, 2))
    if (max(low) >= .1 * supply or min(high) <= .9 * supply
            or any(not -.05 * supply < row[2] < 1.05 * supply
                   or not .94 * supply < row[3] <= 1.001 * supply for row in rows)):
        raise ValueError('loaded inverter truth, settling, headroom or shared-rail limit failed')
    return dict(maximum_low_V=max(low), minimum_high_V=min(high),
                minimum_supply_V=min(row[3] for row in rows))


def run(args):
    work = args.work.resolve()
    work.mkdir(parents=True, exist_ok=True)
    pdk = args.pdk.resolve()
    tech = pdk / 'ihp-sg13g2/libs.tech/klayout/tech'
    models = pdk / 'ihp-sg13g2/libs.tech/ngspice/models'
    root_model = ANALOG / 'basic/generated/cmos_channel.va'
    hashes = source_dependencies([SOURCE.with_suffix('.cpp')], ANALOG.parent, args.cxx,
        ['-std=c++20', '-I' + str(ANALOG), '-I' + str(ANALOG / 'include')])
    tracked = [SOURCE, SOURCE.parents[1] / 'inverter_layout.py', SOURCE.parents[1] / 'mos_layout.py',
        root_model, args.bench.resolve(), args.ngspice.resolve(), args.openvaf.resolve(),
        ANALOG / 'VerilogA/compare.py', ANALOG / 'pdk/1G_repeater_IHPSG13G2/checks.py']
    tracked += [p for p in (pdk / 'ihp-sg13g2/libs.tech/ngspice').rglob('*')
                if p.is_file() and p.suffix in ('.spice', '.lib', '.osdi')]
    tracked += [p for p in tech.rglob('*') if p.is_file() and p.suffix in ('.lvs', '.drc', '.json', '.py')]
    pcell = pdk / 'ihp-sg13g2/libs.tech/klayout/python/sg13g2_pycell_lib'
    tracked += [p for p in pcell.rglob('*') if p.is_file() and p.suffix in ('.py', '.json')]
    tracked += [models.parent / '.spiceinit', args.tools.resolve() / 'root/usr/bin/klayout']
    hashes.update({str(p): digest(p) for p in tracked})
    report = dict(status='running', source_sha256=hashes, generic_flows={}, process_cases={},
        full_chip_drc=False, scope='C++ IC and Verilog-A comparison; independently checked PSP schematic; '
            'routed topology/local DRC/tap LVS; unfilled global density and extracted performance remain unqualified')
    destination = work / 'checks.json'
    tool_environment = dict(os.environ, OMP_THREAD_LIMIT='1')

    def save():
        destination.write_text(json.dumps(report, indent=2) + '\n')

    def command(case, label, argv, environment=None, timeout=180, fatal=True):
        with (case / (label + '.log')).open('w') as log:
            # This fixed two-MOS circuit cannot grow with PHY size. Its
            # measured layout tools use under 450 MiB, and its compiler under
            # 330 MiB; keep larger implementation jobs on the shared slot.
            return execute(list(map(str, argv)), cwd=case, env=environment or tool_environment,
                stdout=log, stderr=-2, check=fatal, timeout=timeout,
                memory_exclusive=False if label in ('compile', 'lvs', 'drc') else None)

    save()
    try:
        case = work / 'generic'
        case.mkdir(exist_ok=True)
        command(case, 'native', [args.bench.resolve(), case, 'run'])
        command(case, 'export', [args.bench.resolve(), case, 'export'])
        command(case, 'compile', [args.openvaf.resolve(), 'design.va', '-o', 'design.osdi'])
        command(case, 'spice', [args.ngspice.resolve(), '-n', '-b', 'bench.cir'])
        header, cpp = read_cpp(case / 'cpp/trace.csv')
        if header != ['time_s', 'A', 'Y', 'VDD']:
            raise ValueError('changed inverter observation interface')
        va = read_spice(case / 'spice.dat', header)
        report['generic_flows'] = {name: contract(rows, 1.2) for name, rows in (('cpp', cpp), ('veriloga', va))}
        difference = max(abs(at(cpp, i * 50e-12, col) - at(va, i * 50e-12, col))
                         for i in range(481) for col in (1, 2, 3))
        report['maximum_generic_solver_difference_V'] = difference
        if difference > .005:
            raise ValueError('independently passing inverter solvers differ by more than 5 mV')
        model, original = (case / 'design.va').read_text(), (case / 'bench.cir').read_text()
        reduced_load, count = re.subn(r'(?m)^(Cdevice\d+ \S+ \S+) 1[.]6e-13 ', r'\1 1.6e-14 ', original)
        if count != 1:
            raise ValueError('changed inverter output-load fixture syntax')
        try:
            process_experiment(model, reduced_load, models, 'mos_tt', 27, 1.2)
        except ValueError:
            pass
        else:
            raise ValueError('PSP binding silently accepted a tenfold smaller output load')
        environment = dict(tool_environment, PDK_ROOT=str(pdk), PDK='ihp-sg13g2',
                           SPICE_USERINIT_DIR=str(models.parent))
        for corner, temperature, supply in itertools.product(
                ('mos_tt', 'mos_ss', 'mos_ff', 'mos_sf', 'mos_fs'), (-40, 27, 125), (1.08, 1.2, 1.32)):
            name = f'{corner}_{temperature}_{supply}'
            case = work / name
            case.mkdir(exist_ok=True)
            (case / 'bench.cir').write_text(process_experiment(model, original, models, corner, temperature, supply))
            command(case, 'spice', [args.ngspice.resolve(), '-b', 'bench.cir'], environment)
            log = (case / 'spice.log').read_text().lower()
            if any(word in log for word in ('timestep too small', 'error', 'fatal', 'not found')):
                raise ValueError('PSP inverter simulation failed: ' + name)
            report['process_cases'][name] = contract(read_spice(case / 'spice.dat', header), supply)
            save()
        layout, top, geometry = create_inverter(pdk, root_model)
        report['layout'] = geometry
        layout.write(str(work / 'inverter.gds'))
        (work / 'inverter.cir').write_text('* C++ IC topology, explicit process geometries\n' + geometry['schematic'])
        physical_environment = dict(os.environ, PATH=str(args.tools.resolve() / 'bin') + os.pathsep + os.environ['PATH'])
        command(work, 'lvs', [sys.executable, tech / 'lvs/run_lvs.py', '--layout', work / 'inverter.gds',
            '--netlist', work / 'inverter.cir', '--topcell', top.name, '--run_mode', 'deep',
            '--run_dir', work / 'lvs'], physical_environment)
        report['lvs'] = check_lvs(work / 'lvs/inverter.lvsdb', top.name, {'A', 'Y', 'VDD', 'VSS'}, 4)
        drc_result = command(work, 'drc', [sys.executable, tech / 'drc/run_drc.py', '--path', work / 'inverter.gds',
            '--topcell', top.name, '--run_dir', work / 'drc', '--mp', 1, '--density_thr', 1, '--antenna'],
            physical_environment, fatal=False)
        report['drc_exit_code'] = drc_result.returncode
        if drc_result.returncode not in (0, 1):
            raise ValueError('DRC runner failed before complete rule evidence')
        drc = work / 'drc/inverter_PhysicalInverter_full.lyrdb'
        report['drc_completion'] = check_drc_completion(drc, tech / 'drc')
        root = ET.parse(drc).getroot()
        categories = {c.findtext('name') for c in root.find('categories').iter('category')}
        if len(categories) < 800 or not {'Ant.a', 'Ant.g', 'Ant.i', 'Pin.a'}.issubset(categories):
            raise ValueError('inverter DRC omits geometry, antenna or pin rule groups')
        violations = Counter(item.findtext('category').strip("'") for item in root.find('items'))
        density_rules = {'AFil.g', 'GFil.g', 'M1.j', 'M2.j', 'M3.j', 'M4.j', 'M5.j', 'TM1.c', 'TM2.c'}
        if set(violations) - density_rules:
            raise ValueError('inverter routing/geometry/antenna/pin violation: ' + str(violations))
        report['local_drc_violations'] = 0
        report['full_drc_pass'] = not violations
        report['unfilled_global_density_violations'] = dict(violations)
        for broken in (root_model.read_text().replace('I(Y, Vss)', 'I(A, Vss)'),
                       root_model.read_text().replace('    analog begin', '    analog begin\nI(Y, Vss) <+ 1;')):
            path = work / 'invalid.va'
            path.write_text(broken)
            try:
                inverter_source(path)
            except ValueError:
                continue
            raise ValueError('layout binding accepted a changed C++ topology or unknown law')
        if any(digest(path) != value for path, value in hashes.items()):
            raise ValueError('inverter source, tools or PDK changed during qualification')
        artifacts = [p for p in work.rglob('*') if p.is_file() and p.suffix in ('.gds', '.cir', '.dat', '.va', '.csv', '.lvsdb', '.lyrdb')]
        report['artifact_sha256'] = {str(p): digest(p) for p in artifacts}
        report['status'] = 'pass'
        print(f'PASS inverter: two generic flows/comparison, {len(report["process_cases"])} PSP cases, '
              f'local DRC/tap LVS, {geometry["bounding_area_um2"]:.3f} um2 bounding area')
    except BaseException as error:
        report.update(status='fail', error=str(error))
        raise
    finally:
        save()


if __name__ == '__main__':
    configure_cleanup()
    parser = argparse.ArgumentParser(description=__doc__)
    for name in ('pdk', 'tools', 'work', 'bench', 'ngspice', 'openvaf'):
        parser.add_argument('--' + name, type=Path, required=True)
    parser.add_argument('--cxx', default='/usr/bin/c++')
    run(parser.parse_args())
