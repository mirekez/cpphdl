#!/usr/bin/env python3
"""Close clock-by-clock SAR feedback through each solver's electrical latch."""
import argparse
import bisect
import csv
import hashlib
import json
import math
from pathlib import Path
import re
import subprocess
import sys

SOURCE = Path(__file__).resolve()
ANALOG = SOURCE.parents[2]
sys.path.insert(0, str(ANALOG / 'VerilogA'))
from live import NgspiceSession
from compare import read_cpp, read_spice
from hardware import configure_cleanup, execute, source_dependencies


def digest(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


def live_flow(args, work, prepare_process=None):
    with (work / 'export.log').open('w') as log:
        execute([str(args.bench.resolve()), str(work), 'export', str(args.slices), '.25', str(args.bursts)],
                stdout=log, stderr=-2, check=True, timeout=30)
    metadata = json.loads((work / 'live.json').read_text())
    low = metadata.get('feedback_low_V', .99)
    high = metadata.get('feedback_high_V', 2.31)
    if not (0 < low < high and (low, high) in ((.99, 2.31), (.36, .84))):
        raise ValueError('unknown electrical decision voltage domain')
    extra_setup = []
    if prepare_process is None:
        with (work / 'compile.log').open('w') as log:
            execute([str(args.openvaf.resolve()), 'design.va', '-o', 'design.osdi'], cwd=work,
                    stdout=log, stderr=-2, check=True, timeout=180)
        experiment = (work / 'bench.cir').read_text()
    else:
        experiment, extra_setup = prepare_process(work, metadata)
    deck, control = experiment.split('.control\n', 1)
    commands = control.splitlines()
    # Only the interactive host changes control sources. The MOS model and
    # every physical component come from the same exported C++ IC hierarchy.
    (work / 'live.cir').write_text(deck + '.end\n')
    start = next(line for line in commands if line.startswith('tran '))
    setup = extra_setup + [line.replace('pre_osdi ', 'osdi ', 1) if line.startswith('pre_osdi ') else line
             for line in commands[:commands.index(start)]]
    output = [line for line in commands if line.startswith(('let observation', 'wrdata '))]
    transition = metadata.get('control_transition_s', 0)
    if not math.isfinite(transition) or not 0 <= transition < metadata['period_s'] / 4:
        raise ValueError('invalid live control transition time')
    driver_waves = {driver['id']: [0, 0 if driver['bit'] == 8 else 1] for driver in metadata['drivers']}
    replay_prefix = metadata.get('replay_prefix', False)
    if replay_prefix and not transition:
        raise ValueError('causal prefix replay requires continuous control edges')

    def replay(spice, stop):
        # Recompute the causal prefix with its complete control history. This
        # avoids PSP's pause/resume failure without transplanting charge states
        # or replaying decisions from another solver.
        prefix = deck
        for driver in metadata['drivers']:
            identifier = driver['id']
            pattern = rf'^Vbit{identifier} bit{identifier} 0 PWL\([^\n]+\)$'
            points = ' '.join(format(x, '.17g') for x in driver_waves[identifier])
            prefix, changed = re.subn(pattern, f'Vbit{identifier} bit{identifier} 0 PWL({points})', prefix, flags=re.M)
            if changed != 1:
                raise ValueError('changed causal-prefix control source')
        (work / 'prefix.cir').write_text(prefix + '.end\n')
        analysis = start.split()
        analysis[2] = format(stop, '.17g')
        spice.command('destroy all\nsource prefix.cir\n' + ' '.join(analysis), timeout=180)
    server_log = (work / 'controller.log').open('w')
    server = subprocess.Popen([str(args.bench.resolve()), 'server', str(args.slices)],
                              stdin=subprocess.PIPE, stdout=subprocess.PIPE, stderr=server_log, text=True)
    try:
        header = server.stdout.readline().strip().split(',')
        if header[:7] != ['clock', 'data', 'valid', 'slice', 'fault', 'acquire', 'convert']:
            raise ValueError('controller server did not provide its interface')

        def read_control():
            line = server.stdout.readline()
            if not line:
                raise RuntimeError('controller server closed before completing')
            fields = list(csv.reader([line]))[0]
            if len(fields) != len(header):
                raise ValueError('controller row has an inconsistent interface')
            return {name: int(value) for name, value in zip(header, fields)}

        previous = read_control()
        controls = [previous]
        decisions = []
        with NgspiceSession(args.ngspice, work, work / 'interactive.log') as spice:
            spice.command('\n'.join(setup) + '\nsource live.cir')
            if not replay_prefix:
                for clock in range(metadata['clocks']):
                    edge = (clock + .5) * metadata['period_s']
                    spice.command(f'stop when time = {edge:.17g}')
                spice.command(start, timeout=180)
            for clock in range(metadata['clocks']):
                edge = (clock + .5) * metadata['period_s']
                sampled = not replay_prefix or bool(previous['convert'])
                if replay_prefix and sampled:
                    replay(spice, edge)

                def last(vector):
                    response = spice.command(f'print {vector}[length({vector})-1]')
                    values = re.findall(r'=\s*([-+0-9.eE]+)', response)
                    if len(values) != 1:
                        raise ValueError('cannot read paused ngspice vector: ' + response)
                    return float(values[0])

                time = last('time') if sampled else edge
                if abs(time - edge) > 1e-15:
                    raise ValueError(f'ngspice missed host edge {clock}: {time} instead of {edge}')
                feedback = 0
                observed = []
                for i, node in enumerate(metadata['decisions']):
                    value = last('v(' + node + ')') if sampled else None
                    observed.append(value)
                    if previous['convert'] & (1 << i):
                        if not math.isfinite(value) or low <= value <= high:
                            raise ValueError(f'unresolved electrical decision at clock {clock}, slice {i}: {value}')
                        feedback |= (value > high) << i
                decisions.append(dict(clock=clock, time_s=time, voltage_V=observed, feedback=feedback, sampled=sampled))
                server.stdin.write(f'{feedback} {int(clock < metadata["launches"])}\n')
                server.stdin.flush()
                current = read_control()
                if current['clock'] != clock:
                    raise ValueError('controller and analog simulator clocks diverged')
                updates = []
                for driver in metadata['drivers']:
                    i, bit = driver['slice'], driver['bit']

                    def drive(row):
                        return (row['acquire'] >> i) & 1 if bit == 8 else 1 - ((row['trial' + str(i)] >> bit) & 1)

                    value = drive(current)
                    if value != drive(previous):
                        if transition:
                            wave = driver_waves[driver['id']]
                            # Keep the new source breakpoint strictly after
                            # the already accepted point of the paused solver.
                            wave.extend([edge + transition / 100, drive(previous), edge + transition, value])
                            updates.append(f'alter @Vbit{driver["id"]}[pwl] = [ ' +
                                           ' '.join(format(x, '.17g') for x in wave) + ' ]')
                        else:
                            updates.append(f'alter Vbit{driver["id"]} {value}')
                if updates and not replay_prefix:
                    spice.command('\n'.join(updates))
                controls.append(current)
                previous = current
                if not replay_prefix:
                    spice.command('resume', timeout=180)
            if replay_prefix:
                replay(spice, float(start.split()[2]))
            spice.command('\n'.join(output), timeout=180)
        with (work / 'controls.csv').open('w') as stream:
            writer = csv.DictWriter(stream, header)
            writer.writeheader()
            writer.writerows(controls)
        (work / 'decisions.json').write_text(json.dumps(decisions, indent=2) + '\n')
        server.stdin.close()
        if server.wait(timeout=5):
            raise ValueError('controller server failed')
    finally:
        if server.poll() is None:
            server.kill()
            server.wait()
        server.stdout.close()
        server_log.close()


def check(work, flow, count, bursts, core=False):
    if flow == 'cpp':
        header, rows = read_cpp(work / 'cpp/trace.csv')
        trace = work / 'cpp/trace.csv'
    else:
        with (work / 'columns.csv').open() as stream:
            header = next(csv.reader(stream))
        trace = work / 'spice.dat'
        rows = read_spice(trace, header)
    conversions = count * bursts
    period, clocks = 4e-9, conversions + (24 if core else 16)
    if (not rows or abs(rows[-1][0] - clocks * period) > 1e-14
            or any(value is None or not math.isfinite(value) for row in rows for value in row)):
        raise ValueError('incomplete/nonfinite electrical ADC trajectory')
    times = [row[0] for row in rows]
    if max(b - a for a, b in zip(times, times[1:])) > period / 128:
        raise ValueError('ADC electrical waveform has insufficient time coverage')

    def at(name, time):
        column = header.index(name)
        i = min(max(1, bisect.bisect_left(times, time)), len(times) - 1)
        a, b = rows[i - 1], rows[i]
        return a[column] + (b[column] - a[column]) * (time - a[0]) / (b[0] - a[0])

    with (work / 'controls.csv').open() as stream:
        controls = [{key: int(value) for key, value in row.items()} for row in csv.DictReader(stream)]
    if [r['clock'] for r in controls] != list(range(-1, clocks)) or any(r['fault'] for r in controls):
        raise ValueError('ADC control trajectory is incomplete or faulted')
    results = [r for r in controls if r['valid']]
    if ([r['clock'] for r in results] != list(range(13, 13 + conversions))
            or [r['slice'] for r in results] != [i % count for i in range(conversions)]):
        raise ValueError('ADC conversion order/cadence differs from its contract')
    codes = [r['data'] for r in results]
    levels = [1.15 + .25 * (((i + 3) * 3) % 5) for i in range(conversions)]
    errors = [abs(code - math.floor(level * 256 / 3.3)) for code, level in zip(codes, levels)]
    hold_errors = []
    for i, level in enumerate(levels):
        start, end = (i + 3.5) * period, (i + 12.5) * period
        hold_errors.extend(abs(at('HELD' + str(i % count), start + 1e-9 + j * (end - start - 2e-9) / 32) - level)
                           for j in range(33))
    supplies = [row[header.index('VDD')] for row in rows if row[0] > 2e-9]
    checks = dict(code_accuracy=max(errors) <= 2, sampling_and_hold=max(hold_errors) < .02,
                  supplied_operation=3.0 < min(supplies) < 3.3 and max(supplies) < 3.35)
    result = dict(codes=codes, expected_levels_V=levels, code_error_counts=errors,
                  maximum_hold_error_V=max(hold_errors), minimum_supply_V=min(supplies), checks=checks,
                  waveform=str(trace), waveform_sha256=digest(trace), controls_sha256=digest(work / 'controls.csv'))
    if core:
        normalized = [r for r in controls if r['normalized_valid']]
        if ([r['clock'] for r in normalized] != list(range(21, 21 + conversions)) or
                [r['normalized_slice'] for r in normalized] != [i % count for i in range(conversions)]):
            raise ValueError('normalized ADC cadence or slice metadata is misaligned')
        expected = []
        for code in codes:
            delta = (code - 128) * 424 / 256
            expected.append(128 + int(math.copysign(math.floor(abs(delta) + .5), delta)))
        actual = [r['normalized'] for r in normalized]
        physical_targets = [128 + round((level - 1.65) / .25) * 32 for level in levels]
        logic = [r[header.index('VLOGIC')] for r in rows if r[0] > 2e-9]
        delivered = True
        # Verify the actual low-voltage node read by each SAR comparison,
        # independently of the host server's recorded integer decision.
        for previous in controls[:-1]:
            time = (previous['clock'] + 1.5) * period
            for i in range(count):
                if previous['convert'] & (1 << i):
                    value = at('CORE_DECISION' + str(i), time)
                    delivered &= value < .36 or value > .84
        checks.update(core_voltage_delivery=bool(delivered), core_supply=min(logic) > 1.08 and max(logic) < 1.35,
                      normalized_codes=actual == expected,
                      normalized_accuracy=max(abs(a - b) for a, b in zip(actual, physical_targets)) <= 4,
                      normalized_unclipped=not any(r['clipped'] for r in controls))
        result.update(normalized_codes=actual, minimum_core_supply_V=min(logic),
                      maximum_normalized_error_counts=max(abs(a - b) for a, b in zip(actual, physical_targets)))
    if not all(checks.values()):
        (work / 'failed_contract.json').write_text(json.dumps(result, indent=2) + '\n')
        raise ValueError('electrical ADC array contract failed: ' + str(checks))
    return result


def run(args):
    work = args.work.resolve()
    work.mkdir(parents=True, exist_ok=True)
    report_path = work / 'checks.json'
    report_path.unlink(missing_ok=True)
    provenance = source_dependencies([SOURCE.parent / 'adc_array.cpp'], ANALOG.parent, args.cxx,
        ['-std=c++20', '-I' + str(ANALOG), '-I' + str(ANALOG / 'include'), '-I/usr/include/eigen3'])
    provenance[str(SOURCE)] = digest(SOURCE)
    provenance[str(ANALOG / 'VerilogA/live.py')] = digest(ANALOG / 'VerilogA/live.py')
    binary_sha256 = {str(args.bench.resolve()): digest(args.bench.resolve())}
    if args.flow == 'veriloga':
        for binary in (args.openvaf, args.ngspice):
            binary_sha256[str(binary.resolve())] = digest(binary.resolve())
    if args.flow == 'cpp':
        with (work / 'native.log').open('w') as log:
            execute([str(args.bench.resolve()), str(work), 'run', str(args.slices), '.25', str(args.bursts)],
                    stdout=log, stderr=-2, check=True, timeout=7200 if args.slices == 16 else 1800)
    else:
        live_flow(args, work)
    result = check(work, args.flow, args.slices, args.bursts)
    if any(digest(Path(path)) != value for path, value in (provenance | binary_sha256).items()):
        raise ValueError('ADC sources changed during validation')
    result.update(status='pass', flow=args.flow, slices=args.slices, bursts=args.bursts, source_sha256=provenance,
                  binary_sha256=binary_sha256,
                  scope='generic MOS array with electrical decision storage and online host-controlled gates; no IHP macro qualification')
    report_path.write_text(json.dumps(result, indent=2) + '\n')
    print('PASS', args.flow, args.slices, 'physical SAR slices:', result['codes'])


def compare(args):
    work = args.work.resolve()
    reports = []
    for flow in ('cpp', 'veriloga'):
        directory = work / flow
        report = json.loads((directory / 'checks.json').read_text())
        if (report.get('status') != 'pass' or report.get('slices') != args.slices
                or report.get('bursts') != args.bursts or report.get('flow') != flow):
            raise ValueError('ADC comparison requires independently passing flows')
        for path, expected in (report['source_sha256'] | report['binary_sha256']).items():
            if digest(Path(path)) != expected:
                raise ValueError('ADC comparison inputs are stale: ' + path)
        # Rerun the common absolute contract before comparing outputs.
        current = check(directory, flow, args.slices, args.bursts)
        if any(current[key] != report[key] for key in current):
            raise ValueError('ADC flow evidence changed before comparison')
        reports.append(report)
    if reports[0]['source_sha256'] != reports[1]['source_sha256']:
        raise ValueError('ADC solvers did not use the same circuit/controller/checker sources')
    differences = [abs(a - b) for a, b in zip(reports[0]['codes'], reports[1]['codes'])]
    if len(differences) != args.slices * args.bursts or max(differences) > 1:
        raise ValueError('ADC electrical solvers differ by more than one count')
    result = dict(status='pass', slices=args.slices, bursts=args.bursts, code_differences=differences,
                  flow_report_sha256={flow: digest(work / flow / 'checks.json') for flow in ('cpp', 'veriloga')},
                  scope='finite interleaved electrical acquisition, including slice reuse when bursts > 1; no IHP/layout qualification')
    (work / 'comparison.json').write_text(json.dumps(result, indent=2) + '\n')
    print('PASS independently checked ADC flow comparison:', differences)


if __name__ == '__main__':
    configure_cleanup()
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--bench', type=Path)
    parser.add_argument('--work', type=Path, required=True)
    parser.add_argument('--flow', choices=('cpp', 'veriloga', 'compare'), required=True)
    parser.add_argument('--slices', type=int, choices=(1, 4, 16), required=True)
    parser.add_argument('--bursts', type=int, choices=range(1, 17), default=1)
    parser.add_argument('--openvaf', type=Path)
    parser.add_argument('--ngspice', type=Path)
    parser.add_argument('--cxx', default='/usr/bin/c++')
    args = parser.parse_args()
    if args.bursts != 1 and args.slices != 16:
        parser.error('repeated operation requires sixteen physical slices')
    if args.flow == 'veriloga' and (not args.openvaf or not args.ngspice):
        parser.error('Verilog-A flow requires openvaf and ngspice')
    if args.flow != 'compare' and not args.bench:
        parser.error('electrical flows require the bench')
    compare(args) if args.flow == 'compare' else run(args)
