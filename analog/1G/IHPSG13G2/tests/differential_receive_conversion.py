"""Check closed conversion after front-end warmup in native, VA and PSP flows."""
import argparse
import csv
import json
import math
import hashlib
import re
from bisect import bisect_left, bisect_right
from pathlib import Path
import sys
import tempfile

SOURCE = Path(__file__).resolve()
ANALOG = SOURCE.parents[3]
sys.path[:0] = [str(SOURCE.parents[1]), str(ANALOG / '1G/tests'), str(ANALOG / 'VerilogA')]
from hardware import configure_cleanup, execute, source_dependencies
from programmable_receive_array import read_cpp, read_spice, digest
from mos_geometry import size_mos_instances
from programmable_receive_gain import windowed_deck
from time_grid import windowed_transient, check_observed_grid
from trace_storage import capture_trace
import xspice_live


def contract(header, rows, controls, metadata):
    count, launches, clocks, period = (metadata[k] for k in ('slices', 'launches', 'clocks', 'period_s'))
    origin = metadata['clock_origin_s']
    times = [row[0] for row in rows]
    if (len(rows) < 100 or times[0] > origin + 20e-12 or abs(times[-1] - origin - clocks * period) > 1e-14
            or any(b < a for a, b in zip(times, times[1:])) or any(not all(math.isfinite(v) for v in row) for row in rows)):
        raise ValueError('incomplete programmable conversion trajectory')
    if [row['clock'] for row in controls] != list(range(-1, clocks)) or any(row['fault'] for row in controls):
        raise ValueError('missing controller clocks or conversion fault')

    def at(name, time):
        right = bisect_left(times, time)
        if not 0 < right < len(rows):
            raise ValueError('conversion aperture outside the observation window')
        a, b = rows[right - 1], rows[right]
        column = header.index(name)
        return a[column] + (b[column] - a[column]) * (time - a[0]) / (b[0] - a[0])

    raw = [row for row in controls if row['valid']]
    normalized = [row for row in controls if row['normalized_valid']]
    for records, delay, identity in ((raw, 13, 'slice'), (normalized, 21, 'normalized_slice')):
        if ([r['clock'] for r in records] != list(range(delay, delay + launches))
                or [r[identity] for r in records] != [i % count for i in range(launches)]):
            raise ValueError('wrong conversion cadence or slice identity')
    samples = []
    delivery = []
    held_ranges = []
    for index, record in enumerate(raw):
        close = origin + (index + 3.5) * period
        target = at('GAIN_OUTPUT', close)
        supply = at('VDD', close)
        column = header.index('HELD' + str(index % count))
        end = origin + (index + 12.5) * period
        window = rows[bisect_left(times, close + 1e-9):bisect_right(times, end + 25e-12)]
        if len(window) < 10:
            raise ValueError('missing complete held-conversion window')
        held = [row[column] for row in window]
        held_ranges.append([min(held), max(held)])
        expected = math.floor(target * 256 / supply)
        samples.append(dict(slice=index % count, target_V=target, code=record['data'], expected_code=expected,
                            code_error=abs(record['data'] - expected), acquisition_V=abs(held[0] - target),
                            hold_variation_V=max(held) - min(held)))
        aperture = close + .5e-9
        sense = at('PLUS', aperture) - at('MINUS', aperture)
        line = at('MDI_P', aperture) - at('MDI_N', aperture)
        delivery.append(abs(sense - .02125 * line))
    expected_normalized = []
    for record in raw:
        value = (record['data'] - metadata['normalizer_offset']) * 424 / 256
        expected_normalized.append(max(0, min(255, 128 + int(math.copysign(math.floor(abs(value) + .5), value)))))
    vdd = [r[header.index('VDD')] for r in rows]
    core = [r[header.index('VLOGIC')] for r in rows]
    signal = [r[header.index('GAIN_OUTPUT')] for r in rows]
    delivered = True
    for previous in controls[:-1]:
        time = origin + (previous['clock'] + 1.5) * period
        for slice in range(count):
            if previous['convert'] & (1 << slice):
                column = header.index('CORE_DECISION' + str(slice))
                values = [at('CORE_DECISION' + str(slice), time + delta) for delta in (-25e-12, 0, 25e-12)]
                values += [r[column] for r in rows[bisect_left(times, time - 25e-12):bisect_right(times, time + 25e-12)]]
                bit = values[1] > .84
                delivered &= all((v < .36 or v > .84) and (v > .84) == bit for v in values)
    initial = rows[0]
    precharged = (metadata.get('precharge') is True and
        controls[0]['acquire'] & ((1 << count) - 1) == (1 << count) - 1 and
        all(initial[header.index('TRACK' + str(i))] >= .9 * initial[header.index('VDD')] and
            abs(initial[header.index('HELD' + str(i))] - initial[header.index('GAIN_OUTPUT')]) <= .020
            for i in range(count)))
    checks = dict(physical_precharge=precharged, input_delivery=max(delivery) <= .002,
                  conversion_accuracy=max(s['code_error'] for s in samples) <= 2,
                  sampler_acquisition=max(s['acquisition_V'] for s in samples) <= .020,
                  hold=max(s['hold_variation_V'] for s in samples) <= .020,
                  captured_headroom=all(.85 <= sample['target_V'] <= 2.05 for sample in samples),
                  held_headroom=all(.85 <= low <= high <= 2.05 for low, high in held_ranges),
                  terminal_range=all(0 <= value <= rail for value, rail in zip(signal, vdd)),
                  dynamic_excitation=max(signal) - min(signal) >= .03,
                  io_supply=min(vdd) >= 3 and max(vdd) <= 3.35,
                  core_supply=min(core) >= 1.08 and max(core) <= 1.35,
                  core_decisions_delivered=bool(delivered),
                  normalization=[r['normalized'] for r in normalized] == expected_normalized,
                  unclipped=not any(r['clipped'] for r in normalized))
    # The ADC's functional input range applies at capture and throughout
    # conversion. Track/settle excursions are characterized separately and
    # remain constrained by the physical supply rails.
    return dict(checks=checks, samples=samples, held_ranges_V=held_ranges,
                gain_output_range_V=[min(signal), max(signal)],
                gain_output_outside_conversion_range_states=sum(not .85 <= value <= 2.05 for value in signal), codes=[r['data'] for r in raw],
                maximum_acquisition_error_V=max(s['acquisition_V'] for s in samples),
                maximum_hold_variation_V=max(s['hold_variation_V'] for s in samples),
                maximum_code_error=max(s['code_error'] for s in samples))


def run(args):
    root = args.work.resolve() / args.flow
    root.mkdir(parents=True, exist_ok=True)
    (root.parent / 'comparison.json').unlink(missing_ok=True)
    case = Path(tempfile.mkdtemp(prefix='run-', dir=root))
    args.export_arguments = [args.slices, args.gain, args.storage_pf, args.bursts]
    actual_pdk = args.pdk
    if args.flow != 'psp':
        args.pdk = None
    report = dict(status='running', flow=args.flow, slices=args.slices, gain=args.gain, storage_pf=args.storage_pf,
                  bursts=args.bursts, work=str(case), fixture_source=str(args.fixture_source.resolve()),
                  corner=args.corner, temperature_C=args.temperature, scope='cold-start MDI network, physical range reference, differential front end and 13-position C++RTL controller bank closed around real SAR circuits; nominal passives, no physical TX/clock/AGC/complete PHY proof')
    hashes = source_dependencies([args.fixture_source.resolve()], ANALOG.parent, args.cxx,
        ['-std=c++20', '-I' + str(ANALOG), '-I' + str(ANALOG / 'include'), '-I/usr/include/eigen3'])
    paths = [SOURCE, args.bench.resolve(), ANALOG / 'VerilogA/time_grid.py', ANALOG / 'VerilogA/trace_storage.py',
             SOURCE.with_name('programmable_receive_array.py'), SOURCE.with_name('programmable_receive_gain.py'),
             SOURCE.parents[1] / 'differential_receive_frontend.hpp', SOURCE.parents[1] / 'receive_reference.hpp',
             SOURCE.parents[1] / 'receive_frontend_process.py', SOURCE.parents[1] / 'mos_geometry.py',
             SOURCE.parents[1] / 'spice_vectors.py', SOURCE.parents[1] / 'xspice_live.py',
             SOURCE.parents[1] / 'xspice_bridges.py']
    paths += [ANALOG / 'VerilogA/compare.py']
    if args.flow != 'cpp':
        installation = args.ngspice.resolve().parents[1]
        include = xspice_live.headers(args.ngspice)
        host = SOURCE.with_name('sim_adc_controller.cpp')
        hashes.update(source_dependencies([host], ANALOG.parent, args.cxx,
            ['-std=c++17', '-DCPPHDL_ADC_COMPACT', '-I' + str(ANALOG.parent / 'include'), '-I' + str(include)]))
        paths += [args.ngspice.resolve(), host, installation / 'share/ngspice/scripts/spinit']
        paths += list((installation / 'lib/ngspice').glob('*.cm')) + list((include / 'ngspice').glob('*.h'))
    if args.flow == 'veriloga':
        paths += [args.driver.resolve(), args.openvaf.resolve(), ANALOG / 'VerilogA/hierarchy.py', ANALOG / 'VerilogA/Driver.cpp']
    if args.flow == 'psp':
        paths += [SOURCE.parents[1] / 'differential_conversion_process.py']
        models = actual_pdk.resolve() / 'ihp-sg13g2/libs.tech/ngspice'
        paths += [models / '.spiceinit'] + [p for p in models.rglob('*') if p.is_file() and p.suffix in ('.lib', '.spice', '.osdi')]
    hashes.update({str(p): digest(p) for p in paths})
    report['source_sha256'] = hashes

    def save():
        (root / 'checks.json').write_text(json.dumps(report, indent=2) + '\n')
        (case / 'checks.json').write_text(json.dumps(report, indent=2) + '\n')

    def prepare(work, metadata):
        if (metadata['slices'], metadata['gain_code'], metadata['sample_storage_F']) != (args.slices, args.gain, args.storage_pf * 1e-12):
            raise ValueError('exported conversion profile differs from requested circuit')
        deck = (work / 'bench.cir').read_text()
        if args.flow == 'psp':
            from differential_conversion_process import process_deck
            if actual_pdk is None:
                raise ValueError('PSP conversion requires the IHP PDK')
            models = actual_pdk.resolve() / 'ihp-sg13g2/libs.tech/ngspice/models'
            deck, inventory = process_deck((work / 'design.va').read_text(), deck, models, metadata,
                                           corner=args.corner, temperature=args.temperature)
            deck, geometry = size_mos_instances(deck)
            (work / 'bindings.json').write_text(json.dumps(dict(devices=inventory, geometry=geometry), indent=2) + '\n')
        deck, changed = re.subn(r'^(Vbit\d+ \S+ 0) DC ([01])$', r'\1 PWL(0 \2 1 \2)', deck, flags=re.M)
        if changed != 9 * args.slices:
            raise ValueError('changed host control initial conditions')
        deck, grid = windowed_transient(windowed_deck(deck), 1e-9)
        # This fixture starts every stored node at zero and ramps both rails.
        # Keep UIC so intrinsic charge follows the actual cold startup.
        (work / 'timing-grid.json').write_text(json.dumps(grid, indent=2) + '\n')
        (work / 'bench.cir').write_text(deck)
        if args.flow == 'veriloga':
            with (work / 'partition.log').open('w') as log:
                execute([str(args.driver.resolve()), '--partition', str(work / 'design.va'), '--output', str(work),
                         '--experiment', str(work / 'bench.cir')], stdout=log, stderr=-2, check=True, timeout=60, memory_exclusive=False)
            hierarchy = json.loads((work / 'AnalogExperiment.hierarchy.json').read_text())
            for master in hierarchy['masters']:
                with (work / (master + '.log')).open('w') as log:
                    execute([str(args.openvaf.resolve()), master + '.va', '-o', master + '.osdi'], cwd=work,
                            stdout=log, stderr=-2, check=True, timeout=180, memory_exclusive=False)
            deck = (work / 'bench.cir').read_text()
        return deck, []

    save()
    try:
        if args.flow == 'cpp':
            with (case / 'export.log').open('w') as log:
                execute([str(args.bench.resolve()), str(case), 'export', *map(str, args.export_arguments)],
                        stdout=log, stderr=-2, check=True, timeout=60)
            with capture_trace(case / 'cpp/trace.csv') as captured, (case / 'run.log').open('w') as log:
                execute([str(args.bench.resolve()), str(case), 'run', *map(str, args.export_arguments)],
                        stdout=log, stderr=-2, check=True, timeout=14400)
            raw = captured.path
            header, rows = read_cpp(raw)
        else:
            with capture_trace(case / 'spice.dat') as captured:
                xspice_live.live_flow(args, case, prepare)
            header = next(csv.reader((case / 'columns.csv').open()))
            raw = captured.path
            rows = read_spice(raw, header)
            report['bridge'] = xspice_live.check_bridge(case, args.slices, lambda path, names: read_spice(raw, names))
            report['observed_grid'] = check_observed_grid(rows, json.loads((case / 'timing-grid.json').read_text()))
        metadata = json.loads((case / 'live.json').read_text())
        report['gain_compensation_F'] = metadata['gain_compensation_F']
        with (case / 'controls.csv').open() as source:
            controls = [{k: int(v) for k, v in row.items()} for row in csv.DictReader(source)]
        report.update(contract(header, rows, controls, metadata))
        report.update(waveform=str(raw), waveform_sha256=digest(raw), waveform_archive=captured.metadata)
        save()
        failed = [k for k, value in report['checks'].items() if not value]
        if failed:
            raise ValueError('programmable conversion failed: ' + ', '.join(failed))
        if any(digest(p) != h for p, h in hashes.items()):
            raise ValueError('conversion sources changed during qualification')
        report['status'] = 'pass'
        report['artifact_sha256'] = {str(p): digest(p) for p in case.iterdir() if p.is_file() and p.name != 'checks.json'}
        print('PASS', args.flow, report['codes'], 'maximum error', report['maximum_code_error'], flush=True)
    except BaseException as error:
        report.update(status='fail', error=str(error))
        raise
    finally:
        save()


if __name__ == '__main__':
    configure_cleanup()
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--flow', choices=('cpp', 'veriloga', 'psp'), required=True)
    parser.add_argument('--bench', type=Path, required=True)
    parser.add_argument('--fixture-source', type=Path, default=SOURCE.with_suffix('.cpp'))
    parser.add_argument('--work', type=Path, required=True)
    parser.add_argument('--slices', type=int, choices=(1, 4, 13), default=1)
    parser.add_argument('--gain', type=int, choices=range(4, 9), default=4)
    parser.add_argument('--storage-pf', type=float, choices=(1, 1.5, 2, 3), default=1.5)
    parser.add_argument('--bursts', type=int, choices=(1, 2), default=1)
    parser.add_argument('--pdk', type=Path)
    parser.add_argument('--ngspice', type=Path)
    parser.add_argument('--driver', type=Path)
    parser.add_argument('--openvaf', type=Path)
    parser.add_argument('--cxx', default='/usr/bin/g++-11')
    parser.add_argument('--corner', choices=('mos_tt', 'mos_ss', 'mos_ff', 'mos_sf', 'mos_fs'), default='mos_tt')
    parser.add_argument('--temperature', type=float, default=27)
    parser.add_argument('--simulation-timeout', type=float)
    args = parser.parse_args()
    if args.flow != 'psp' and (args.corner != 'mos_tt' or args.temperature != 27):
        parser.error('process and temperature selection requires the PSP flow')
    run(args)
