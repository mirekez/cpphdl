#!/usr/bin/env python3
"""Qualify the coupled receiver's acquisition with explicit IHP PSP devices."""
import argparse
import bisect
import gzip
import hashlib
import itertools
import json
import math
import os
from pathlib import Path
import re
import shutil
import sys

SOURCE = Path(__file__).resolve()
ANALOG = SOURCE.parents[3]
sys.path[:0] = [str(SOURCE.parents[1]), str(ANALOG / '1G/tests'), str(ANALOG / 'VerilogA')]
from receive_frontend_process import process_deck, dc_deck
from mos_geometry import size_mos_instances
from spice_vectors import keep_observed_voltages
from hardware import configure_cleanup, execute, source_dependencies
from compare import read_spice


def digest(path):
    with Path(path).open('rb') as stream:
        return hashlib.file_digest(stream, 'sha256').hexdigest()


def contract(rows, count, dc):
    if (not rows or not 0 <= rows[0][0] <= 10e-12
            or abs(rows[-1][0] - (12 + 8 * count) * 1e-9) > 1e-15
            or any(len(row) != 7 + count or not all(math.isfinite(x) for x in row) for row in rows)):
        raise ValueError('incomplete/nonfinite PSP receive trajectory')
    times = [row[0] for row in rows]
    if any(not 0 < b - a <= 10.1e-12 for a, b in zip(times, times[1:])):
        raise ValueError('unordered/undersampled PSP receive trajectory')

    def at(time, column):
        index = min(max(1, bisect.bisect_left(times, time)), len(times) - 1)
        a, b = rows[index - 1], rows[index]
        return a[column] + (b[column] - a[column]) * (time - a[0]) / (b[0] - a[0])

    samples, errors, drift, buffer_errors, drain_errors, input_errors = [], [], [], [], [], []
    for index in range(2 * count):
        label = (index * 3) % 5
        time = (8 + 4 * index) * 1e-9 + 3.8e-9
        expected = dc[label][4]
        held = at(time, 7 + index % count)
        samples.append(held)
        errors.append(abs(held - expected))
        buffer_errors.append(abs(at(time, 4) - expected))
        drain_errors.append(abs(at(time, 3) - dc[label][3]))
        input_errors += [abs(at(time, 1) - (1.65 + .06 * (label - 2))),
                         abs(at(time, 2) - (1.65 - .06 * (label - 2)))]
        end = ((8 + 4 * (index + count)) * 1e-9 if index < count else times[-1]) - .1e-9
        drift += [abs(row[7 + index % count] - held) for row in rows if time <= row[0] <= end]
    steady = [row for row in rows if row[0] >= 8e-9]
    checks = dict(input_delivery=max(input_errors) < .002,
                  amplifier_delivery=max(drain_errors) < .02,
                  buffer_delivery=max(buffer_errors) < .02,
                  acquisition=max(errors) < .02,
                  hold=max(drift, default=0) < .02,
                  io_supply=all(3 < row[5] < 3.3 for row in steady),
                  core_supply=all(1.08 < row[6] < 1.35 for row in steady),
                  output_headroom=all(.3 < row[4] < row[5] - .3 for row in steady))
    return dict(checks=checks, sample_V=samples, maximum_acquisition_error_V=max(errors),
                maximum_hold_drift_V=max(drift, default=0), maximum_buffer_error_V=max(buffer_errors),
                maximum_drain_error_V=max(drain_errors),
                minimum_io_supply_V=min(row[5] for row in steady),
                minimum_core_supply_V=min(row[6] for row in steady))


def run(args):
    work = args.work.resolve()
    work.mkdir(parents=True, exist_ok=True)
    (work / 'checks.json').unlink(missing_ok=True)
    model_root = args.pdk.resolve() / 'ihp-sg13g2/libs.tech/ngspice'
    paths = [SOURCE, SOURCE.parents[1] / 'receive_frontend_process.py', SOURCE.parents[1] / 'mos_geometry.py',
             SOURCE.parents[1] / 'spice_vectors.py',
             ANALOG / 'VerilogA/compare.py', args.bench.resolve(), args.ngspice.resolve(), model_root / '.spiceinit']
    paths += [path for path in model_root.rglob('*') if path.is_file() and path.suffix in ('.lib', '.spice', '.osdi')]
    hashes = source_dependencies([args.fixture_source.resolve()], ANALOG.parent, args.cxx,
        ['-std=c++20', '-I' + str(ANALOG), '-I' + str(ANALOG / 'include'), '-I/usr/include/eigen3'])
    hashes.update({str(path): digest(path) for path in paths})
    report = dict(status='running', cases={}, source_sha256=hashes,
                  same_equations_as_square_law=False, fixture_source=str(args.fixture_source.resolve()),
                  initialization='retained external capacitor/node ICs, bias-initialized PSP internal charge',
                  scope='explicit PSP receiver acquisition at 3.3/1.2 V; selected engineering sizes, '
                        'external controls and precharge, no closed SAR/PCS, mismatch or layout qualification')
    if args.pvt:
        nominal_path = (args.nominal_report or work.parent / 'receive_frontend_precision_nominal/checks.json').resolve()
        nominal = json.loads(nominal_path.read_text())
        expected_cases = {f'mos_tt_27_{count}' for count in (1, 4, 16)}
        if (nominal.get('status') != 'pass' or nominal.get('source_sha256') != hashes
                or set(nominal.get('cases', {})) != expected_cases):
            raise ValueError('a current complete nominal receive process pass is required before the extended sweep')
        for result in nominal['cases'].values():
            if (not all(result['checks'].values()) or not all(result['dc_checks'].values()) or
                    any(digest(path) != value for path, value in result['artifact_sha256'].items())):
                raise ValueError('nominal receive process evidence is failed or changed')
        report['nominal_evidence'] = dict(path=str(nominal_path), sha256=digest(nominal_path))
        hashes[str(nominal_path)] = digest(nominal_path)
    environment = dict(os.environ, PDK_ROOT=str(args.pdk.resolve()), PDK='ihp-sg13g2',
                       SPICE_USERINIT_DIR=str(model_root))
    corners = ('mos_tt', 'mos_ss', 'mos_ff', 'mos_sf', 'mos_fs') if args.pvt else ('mos_tt',)
    temperatures = (-40, 27, 125) if args.pvt else (27,)

    def simulate(case, label):
        with (case / (label + '.log')).open('w') as log:
            execute([str(args.ngspice.resolve()), '-b', label + '.cir'], cwd=case, env=environment,
                    stdout=log, stderr=-2, check=True, timeout=1800)
        if re.search(r'\b(error|fatal|unknown|failed|aborted)\b|not found|timestep too small',
                     (case / (label + '.log')).read_text(), re.I):
            raise ValueError('PSP receive simulation failed: ' + str(case))

    try:
        for corner, temperature, count in itertools.product(corners, temperatures, (1, 4, 16)):
            name = f'{corner}_{temperature}_{count}'
            case = work / name
            case.mkdir(exist_ok=True)
            with (case / 'export.log').open('w') as log:
                execute([str(args.bench.resolve()), str(case), 'export', str(count), '.05', '10'],
                        stdout=log, stderr=-2, check=True, timeout=60)
            deck, inventory = process_deck((case / 'design.va').read_text(), (case / 'bench.cir').read_text(),
                                           model_root / 'models', count, corner, temperature)
            deck, geometry = size_mos_instances(deck)
            # Bias initialization honors the retained .ic node voltages while
            # initializing the PSP model's additional intrinsic charge states.
            # Skipping it with UIC failed at 0.1 ps on the four-slice circuit.
            deck, initialized = re.subn(r'(?m)^(tran .+) uic$', r'\1', deck)
            if initialized != 1:
                raise ValueError('changed receive transient initialization interface')
            deck = keep_observed_voltages(deck)
            (case / 'ihp.cir').write_text(deck)
            (case / 'dc.cir').write_text(dc_deck(deck))
            (case / 'binding.json').write_text(json.dumps(dict(devices=inventory, geometry=geometry), indent=2) + '\n')
            simulate(case, 'dc')
            with (case / 'dc.dat').open() as stream:
                next(stream)
                dc = [[float(x) for x in line.split()] for line in stream if line.strip()]
            if len(dc) != 5 or any(len(row) != 7 or not all(math.isfinite(x) for x in row) for row in dc):
                raise ValueError('incomplete/nonfinite PSP receive DC sweep')
            dc_checks = dict(input_sweep=all(abs(row[0] - (-.24 + .12 * i)) < 1e-9 and
                abs(row[1] - (1.65 + row[0] / 2)) < .002 and abs(row[2] - (1.65 - row[0] / 2)) < .002
                for i, row in enumerate(dc)),
                distinct_levels=all(b[4] - a[4] > .05 for a, b in zip(dc, dc[1:])),
                adc_input_range=all(1.15 < row[4] < 2.15 for row in dc),
                headroom=all(row[3] - row[4] > .5 and
                             row[3] > max(row[1], row[2]) + .3 and row[5] > row[3] for row in dc))
            if not all(dc_checks.values()):
                raise ValueError('PSP receive DC contract: ' + str(dc_checks))
            simulate(case, 'ihp')
            header = ['time_s', 'PLUS', 'MINUS', 'DRAIN', 'BUFFER', 'VDD', 'VLOGIC']
            header += ['HELD' + str(i) for i in range(count)]
            raw = case / 'spice.dat'
            rows = read_spice(raw, header)
            expected_initial = [1.65, 1.65, 3.3, 1.65, 3.3, 0] + [1.65] * count
            if rows[0][0] != 0 or any(abs(a - b) > 1e-8 for a, b in zip(rows[0][1:], expected_initial)):
                raise ValueError('PSP bias initialization changed the specified external initial voltages')
            result = contract(rows, count, dc)
            result.update(dc_checks=dc_checks, dc_levels_V=[row[4] for row in dc])
            if not all(result['checks'].values()):
                (case / 'failed_contract.json').write_text(json.dumps(result, indent=2) + '\n')
                raise ValueError('PSP receive acquisition contract: ' + str(result['checks']))
            archive = case / 'spice.dat.gz'
            original_sha = digest(raw)
            with raw.open('rb') as source, gzip.open(archive, 'wb', compresslevel=1) as target:
                shutil.copyfileobj(source, target)
            with gzip.open(archive, 'rb') as source:
                if hashlib.file_digest(source, 'sha256').hexdigest() != original_sha:
                    raise ValueError('PSP receive archive is not lossless')
            raw.unlink()
            result['artifact_sha256'] = {str(path): digest(path) for path in
                (archive, case / 'dc.dat', case / 'dc.cir', case / 'ihp.cir', case / 'binding.json')}
            report['cases'][name] = result
            (work / 'checks.json').write_text(json.dumps(report, indent=2) + '\n')
            print(name + ': pass', flush=True)
        if any(digest(path) != value for path, value in hashes.items()):
            raise ValueError('PSP receive inputs changed during qualification')
        report['status'] = 'pass'
    except BaseException as error:
        report.update(status='fail', error=str(error))
        raise
    finally:
        (work / 'checks.json').write_text(json.dumps(report, indent=2) + '\n')


if __name__ == '__main__':
    configure_cleanup()
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--work', type=Path, required=True)
    parser.add_argument('--bench', type=Path, required=True)
    parser.add_argument('--fixture-source', type=Path, default=ANALOG / '1G/tests/receive_frontend.cpp')
    parser.add_argument('--pdk', type=Path, required=True)
    parser.add_argument('--ngspice', type=Path, required=True)
    parser.add_argument('--cxx', default='/usr/bin/c++')
    parser.add_argument('--process-temperature', dest='pvt', action='store_true')
    parser.add_argument('--nominal-report', type=Path)
    run(parser.parse_args())
