#!/usr/bin/env python3
"""Check two C++-declared output stages sharing a line and a finite supply.

Extract the reviewed output-stage devices from the actual C++ export. The
controller and translators have separate transient tests. Here 625 DC points
cover every pair of current/history states; capacitors have zero DC current.
"""
import argparse
import json
import math
import os
from pathlib import Path
import re
import sys
import tempfile

SOURCE = Path(__file__).resolve()
ANALOG = SOURCE.parents[3]
sys.path[:0] = [str(SOURCE.parents[1]), str(ANALOG / '1G/tests')]
from core_driver_process import process_deck as original_binding
from cascoded_driver_process import process_deck as cascoded_binding
from mos_storage_driver_process import process_deck as mos_storage_binding
from core_transmit_driver import HEADER, TRIM_HEADER, qualified_controls
from hardware import execute, source_dependencies, configure_cleanup
from mos_geometry import size_mos_instances
from transmit_driver import digest


def output_stage(model, bench, models, profile, corner, temperature):
    bind = {'cascoded': cascoded_binding, 'mos_storage': mos_storage_binding, 'original': original_binding}[profile]
    _, binding = bind(model, bench, models, corner, programmable=True, temperature=temperature)
    roles = {'current_reference', 'trim_switch', 'main_sink', 'history_sink', 'steering',
             'main_steering', 'history_steering', 'main_current_cascode', 'history_current_cascode'}
    devices = [d for d in binding['devices'] if d.get('role') in roles]
    if len(devices) != (31 if profile == 'original' else 39):
        raise ValueError('unexpected output-stage MOS inventory')
    nodes = {d[key] for d in devices for key in ('a', 'b', 'gate', 'body')}
    observed = dict((int(i), node) for i, node in re.findall(r'^let observation(\d+) = v\((n\d+)\)$', bench, re.M))
    header = HEADER + TRIM_HEADER
    pins = {name: observed[header.index(name) - 1]
            for name in ['P', 'N', 'VDD'] + [f'G{bank}_{bit}' for bank in range(4) for bit in range(4)]
                         + [f'TRIM_HIGH{bit}' for bit in range(6)]}
    resistors = [d for d in binding['devices'] if d['kind'] == 'R' and d['a'] in nodes and d['b'] in nodes
                 and not (d['value'] == 100 and {d['a'], d['b']} == {pins['P'], pins['N']})]
    if len(resistors) != (9 if profile == 'original' else 11):
        raise ValueError('unexpected output-stage resistor inventory')
    for name in ('P', 'N'):
        if sum(d['value'] == 50 and {d['a'], d['b']} == {pins[name], pins['VDD']} for d in resistors) != 1:
            raise ValueError('Norton measurement requires both declared 50-ohm pullups')
    storage = {key: binding[key] for key in ('bias_storage', 'cascode_storage')} if profile == 'mos_storage' else None
    return devices, resistors, pins, storage


def coupled_deck(devices, resistors, pins, models, corner, temperature, trim, storage=None):
    lines = ['Two C++ output stages sharing one differential line',
             f'.lib "{models / "cornerMOShv.lib"}" {corner}', f'.temp {temperature}',
             'Vsupply source 0 3.3', 'Rshared source vdd .25',
             'Vsymbola symbola 0 0', 'Vsymbolb symbolb 0 0']
    if storage is not None:
        lines.append(f'.lib "{models / "cornerMOSCAP.lib"}" moscap_tt')
    for name in ('a', 'b'):
        def node(value):
            return '0' if value == '0' else 'vdd' if value == pins['VDD'] else name + '_' + value
        for index, device in enumerate(devices):
            lines.append(f'X{name}{index} {node(device["a"])} {node(device["gate"])} {node(device["b"])} '
                         f'{node(device["body"])} {device["model"]} w={device["width_um"]}u l={device["length_um"]}u')
        for index, device in enumerate(resistors):
            lines.append(f'R{name}{index} {node(device["a"])} {node(device["b"])} {device["value"]:.17g}')
        if storage is not None:
            # Retain both actual storage models, including their DC leakage.
            # Their remaining dynamic role is checked in the full transient.
            bias = storage['bias_storage']
            cascode = storage['cascode_storage']
            lines += [f'X{name}biasStorage {node(bias["node"])} 0 sg13_moscap_n w=30u l=30u m=40',
                      f'X{name}cascodeStorage 0 {node(cascode["node"])} 0 0 sg13_hv_nmos w=100u l=.5u ng=10 m=220']
        lines += [f'V{name}p {node(pins["P"])} p 0', f'V{name}n {node(pins["N"])} n 0']
        for bank in range(4):
            current = f'floor(v(symbol{name})/5)' if bank < 2 else f'(v(symbol{name})-5*floor(v(symbol{name})/5))'
            relation = '>' if bank % 2 == 0 else '<'
            for bit in range(4):
                gate = pins[f'G{bank}_{bit}']
                lines.append(f'B{name}g{bank}_{bit} {node(gate)} 0 V=v(vdd)*({current} {relation} {bit + .5})')
        for bit in range(6):
            value = '0' if trim & (1 << bit) else 'v(vdd)'
            lines.append(f'B{name}trim{bit} {node(pins[f"TRIM_HIGH{bit}"])} 0 V={value}')
    lines += ['.control', 'set num_threads=1', 'set wr_singlescale', 'set wr_vecnames', 'set numdgt=17',
              'dc Vsymbola 0 24 1 Vsymbolb 0 24 1',
              'wrdata dc.dat v(p) v(n) i(Vap) i(Van) i(Vbp) i(Vbn) v(vdd) i(Vsupply)',
              'quit', '.endc', '.end']
    return size_mos_instances('\n'.join(lines) + '\n')


def check(rows):
    if len(rows) != 625 or any(len(row) != 9 or not all(math.isfinite(v) for v in row) for row in rows):
        raise ValueError('incomplete/nonfinite interaction matrix')
    maximum, worst = 0., None
    expected = lambda state: -.125 * (3 * (state // 5 - 2) + state % 5 - 2)
    for index, row in enumerate(rows):
        a, b = index % 25, index // 25
        if row[0] != a or not 3.05 < row[7] <= 3.3 or not .070 < -row[8] < .100:
            raise ValueError('wrong interaction ordering or supply/current outside limits')
        if min(row[1:3]) < -.1 or max(row[1:3]) > row[7] + .1:
            raise ValueError('physical line voltage outside supplied rails')
        difference = row[1] - row[2]
        # Each stage's pullups also respond to the shared line. Subtract their
        # known conductance from the measured terminal currents to recover the
        # standalone 100-ohm-loaded voltage contribution of that stage.
        contributions = [25 * (row[3] - row[4]) + .5 * difference,
                         25 * (row[5] - row[6]) + .5 * difference]
        if abs(sum(contributions) - difference) > 1e-8:
            raise ValueError('two-driver current conservation failed')
        errors = [abs(x - y) for x, y in zip(contributions, (expected(a), expected(b)))]
        if max(errors) > maximum:
            maximum = max(errors)
            worst = dict(a=a, b=b, line_V=difference, contributions_V=contributions,
                         expected_V=[expected(a), expected(b)])
    return dict(maximum_per_driver_error_V=maximum, worst=worst, matrix_points=625,
                within_existing_20mV_limit=maximum < .020, shared_supply_resistance_ohm=.25)


def run(args):
    root = args.work.resolve()
    root.mkdir(parents=True, exist_ok=True)
    work = Path(tempfile.mkdtemp(prefix='dc-', dir=root))
    stimulus = qualified_controls(args.fixture.resolve())
    models = args.pdk.resolve() / 'ihp-sg13g2/libs.tech/ngspice'
    paths = [SOURCE, SOURCE.parents[1] / 'core_driver_process.py', SOURCE.parents[1] / 'cascoded_driver_process.py',
             SOURCE.parents[1] / 'moscap_driver_process.py', SOURCE.parents[1] / 'mos_storage_driver_process.py',
             SOURCE.parents[1] / 'mos_geometry.py', args.bench.resolve(), args.ngspice.resolve(),
             args.fixture.resolve(), stimulus, models / '.spiceinit']
    paths += [p for p in models.rglob('*') if p.is_file() and p.suffix in ('.lib', '.spice', '.osdi')]
    hashes = source_dependencies([args.fixture_source.resolve()], ANALOG.parent, args.cxx,
        ['-std=c++20', '-I' + str(ANALOG), '-I' + str(ANALOG / 'include'), '-I/usr/include/eigen3'])
    hashes |= {str(path): digest(path) for path in paths}
    report = dict(status='running', profile=args.profile, corner=args.corner, temperature_C=args.temperature,
                  trim_code=args.trim_code, source_sha256=hashes, work=str(work),
                  expected_contract_pass=not args.expect_failure,
                  scope='PSP DC matrix of two exported output stages; shared finite supply, ideal static gate controls and nominal passives; no transient, translator or full-PHY qualification')
    try:
        with (work / 'export.log').open('w') as log:
            execute([str(args.bench.resolve()), str(work), 'export', str(stimulus), '100', '0', str(args.trim_code)],
                    stdout=log, stderr=-2, check=True, timeout=60)
        devices, resistors, pins, storage = output_stage((work / 'design.va').read_text(), (work / 'bench.cir').read_text(),
                                               models / 'models', args.profile, args.corner, args.temperature)
        deck, geometry = coupled_deck(devices, resistors, pins, models / 'models', args.corner,
                                      args.temperature, args.trim_code, storage)
        report['storage_realization'] = storage
        (work / 'physical.cir').write_text(deck)
        (work / 'geometry.json').write_text(json.dumps(geometry, indent=2) + '\n')
        environment = dict(os.environ, PDK_ROOT=str(args.pdk.resolve()), PDK='ihp-sg13g2',
                           SPICE_USERINIT_DIR=str(models), OMP_THREAD_LIMIT='1')
        with (work / 'ngspice.log').open('w') as log:
            execute([str(args.ngspice.resolve()), '-b', 'physical.cir'], cwd=work, env=environment,
                    stdout=log, stderr=-2, check=True, timeout=1200)
        text = (work / 'ngspice.log').read_text()
        if ('ngspice-47 done' not in text or 'No. of Data Rows : 625' not in text
                or re.search(r'\b(error|fatal|aborted|unknown)\b|timestep too small', text, re.I)):
            raise ValueError('incomplete/failed interaction simulation')
        rows = [[float(v) for v in line.split()] for line in (work / 'dc.dat').read_text().splitlines()[1:]]
        report.update(check(rows))
        if report['within_existing_20mV_limit'] == args.expect_failure:
            raise ValueError('unexpected two-driver contract result')
        if args.expect_failure and args.profile != 'original':
            raise ValueError('negative control is restricted to the original output stage')
        if any(digest(Path(path)) != value for path, value in hashes.items()):
            raise ValueError('interaction inputs changed during verification')
        report['artifact_sha256'] = {str(p): digest(p) for p in work.iterdir() if p.is_file()}
        report['status'] = 'pass'
        print('PASS', args.profile, args.corner, 'observed per-driver error V', report['maximum_per_driver_error_V'], flush=True)
    except BaseException as error:
        report.update(status='fail', error=str(error))
        raise
    finally:
        text = json.dumps(report, indent=2) + '\n'
        (work / 'checks.json').write_text(text)
        (root / 'checks.json').write_text(text)


if __name__ == '__main__':
    configure_cleanup()
    parser = argparse.ArgumentParser(description=__doc__)
    for name in ('work', 'fixture', 'fixture-source', 'bench', 'pdk', 'ngspice'):
        parser.add_argument('--' + name, type=Path, required=True)
    parser.add_argument('--profile', choices=('original', 'cascoded', 'mos_storage'), required=True)
    parser.add_argument('--corner', choices=('mos_tt', 'mos_ss', 'mos_ff'), default='mos_tt')
    parser.add_argument('--temperature', type=float, default=27)
    parser.add_argument('--trim-code', type=int, choices=range(64), required=True)
    parser.add_argument('--cxx', default='/usr/bin/c++')
    parser.add_argument('--expect-failure', action='store_true')
    run(parser.parse_args())
