#!/usr/bin/env python3
"""Qualify the connected PLL/translator and clock a real controller from OUT."""
import argparse
import bisect
import csv
import gzip
import hashlib
import json
from pathlib import Path
import shutil
import statistics
import subprocess
import sys

SOURCE = Path(__file__).resolve()
ANALOG = SOURCE.parents[2]
sys.path.insert(0, str(ANALOG / 'VerilogA'))
from compare import checked_rows
from live import NgspiceSession, NgspiceVoltageClock
from hardware import configure_cleanup, execute, source_dependencies

HEADER = ['time_s', 'LOW', 'OUT', 'OUT_N', 'DIVIDED', 'REF', 'CONTROL', 'VLOGIC', 'VDD', 'BIAS', 'UP', 'DOWN',
          'PREDRIVE', 'BUFFERED']
CASES = dict(nominal=(0, False), loaded=(1, False), overloaded=(2, False), refined=(0, True))


def digest(path):
    value = hashlib.sha256()
    with path.open('rb') as file:
        for block in iter(lambda: file.read(1024 * 1024), b''):
            value.update(block)
    return value.hexdigest()


def read(path, flow):
    with (gzip.open(path, 'rt') if path.suffix == '.gz' else path.open()) as file:
        if flow == 'cpp':
            reader = csv.reader(file)
            if next(reader) != HEADER:
                raise ValueError('clock-domain observation interface changed')
        else:
            names = next(file).split()
            if len(names) != len(HEADER) or names[0] != 'time':
                raise ValueError('clock-domain SPICE interface changed')
            reader = (line.split() for line in file if line.strip())
        return checked_rows(reader, len(HEADER), str(path))


def contract(rows, load, refined, controller):
    stop, step = (480e-9, 2.5e-12) if refined else (960e-9, 5e-12)
    times = [row[0] for row in rows]
    if abs(times[-1] - stop) > 1e-14 or max(b - a for a, b in zip(times, times[1:])) > 1.001 * step:
        raise ValueError('incomplete or insufficiently resolved clock-domain trajectory')
    if rows[0][2] > .99:
        raise ValueError('delivered clock did not start low')
    high, count = False, 0
    for row in rows:
        if not high and row[2] >= 2.31:
            high, count = True, count + 1
        elif high and row[2] <= .99:
            high = False
    if type(controller.get('count')) is not int or controller['count'] != count:
        raise ValueError('controller count differs from achieved 3.3 V clock edges')

    def at(time, col):
        i = min(max(1, bisect.bisect_left(times, time)), len(times) - 1)
        a, b = rows[i - 1], rows[i]
        return a[col] + (b[col] - a[col]) * (time - a[0]) / (b[0] - a[0])

    def rising(col, threshold, start, end):
        edges = []
        for a, b in zip(rows, rows[1:]):
            if a[col] < threshold <= b[col]:
                time = a[0] + (threshold - a[col]) * (b[0] - a[0]) / (b[col] - a[col])
                if start < time < end:
                    edges.append(time)
        return edges

    steady = [row for row in rows if row[0] > (100e-9 if refined else 800e-9)]
    if min(row[7] for row in steady) < 1.1 or min(row[8] for row in steady) < 3.05:
        raise ValueError('connected clock supply collapsed')
    if any(not -.1 < row[c] < 3.45 for row in steady for c in (2, 3, 9)):
        raise ValueError('delivered clock or shared bias has unbounded excursions')
    prefix = rising(2, 1.65, 100e-9, 470e-9)
    result = dict(controller_count=count, prefix_edges_s=prefix,
                  prefix_control_V=[at(ns * 1e-9, 6) for ns in range(16, 480)],
                  checks=dict(complete_finite=True, finite_supplies=True, actual_output_clocks_controller=True))
    if refined:
        if len(prefix) < 20:
            raise ValueError('refined connected clock failed to start')
        return result
    low = rising(1, .6, 800e-9, 955e-9)
    divided = rising(4, .6, 800e-9, 959.9e-9)
    reference = rising(5, .6, 790e-9, 960e-9)
    if len(low) < 30 or len(divided) < 15 or len(reference) < 15:
        raise ValueError('connected PLL oscillator, divider or reference stopped')
    frequency = (len(low) - 1) / (low[-1] - low[0])
    divided_frequency = (len(divided) - 1) / (divided[-1] - divided[0])
    phases = [edge - min(reference, key=lambda value: abs(value - edge)) for edge in divided]
    control = [row[6] for row in steady]
    if (abs(frequency / 250e6 - 1) > 500e-6 or abs(divided_frequency / 125e6 - 1) > 500e-6
            or max(abs(value) for value in phases) > 80e-12
            or not .35 < min(control) <= max(control) < 1.0 or max(control) - min(control) > .002):
        raise ValueError('loaded reference PLL lost its frequency/phase/control contract')
    highs = [at(edge + 1.3e-9, 2) for edge in low]
    lows = [at(edge + 3.3e-9, 2) for edge in low]
    rail_pass = min(highs) > 2.8 and max(lows) < .33
    result.update(pll_frequency_Hz=frequency, divided_phase_s=phases,
                  output_minimum_high_V=min(highs), output_maximum_low_V=max(lows),
                  mean_control_V=statistics.mean(control),
                  mean_bias_V=statistics.mean(row[9] for row in steady),
                  checks=result['checks'] | dict(pll_fundamental_rate_and_phase=True))
    if load == 2:
        if rail_pass:
            raise ValueError('20 pF output overload did not break the delivered-clock rail purpose')
        result['checks']['output_overload_visible'] = True
    else:
        if not rail_pass:
            raise ValueError('delivered 3.3 V clock failed to settle to valid rails')
        output = rising(2, 1.65, 800e-9, 959.9e-9)
        if len(output) < 30:
            raise ValueError('translator lost clock cycles')
        delays = [min((edge - value for value in low if 0 <= edge - value < 2e-9), default=2e-9)
                  for edge in output if edge <= low[-1] + 1e-9]
        if not delays or max(delays) > 1e-9:
            raise ValueError('clock translation exceeded the 1 ns delivery budget')
        result.update(output_edges_s=output, maximum_delivery_delay_s=max(delays))
        result['checks']['delivered_clock_rails_and_delay'] = True
    return result


def live(args, work):
    metadata = json.loads((work / 'live.json').read_text())
    deck, control = (work / 'bench.cir').read_text().split('.control\n', 1)
    (work / 'live.cir').write_text(deck + '.end\n')
    commands = control.splitlines()
    start = next(line for line in commands if line.startswith('tran '))
    setup = [line.replace('pre_osdi ', 'osdi ', 1) if line.startswith('pre_osdi ') else line
             for line in commands[:commands.index(start)]]
    output = [line for line in commands if line.startswith(('let observation', 'wrdata '))]
    with (work / 'controller.log').open('w') as log:
        server = subprocess.Popen([str(args.bench.resolve()), 'server'], stdin=subprocess.PIPE,
                                  stdout=subprocess.PIPE, stderr=log, text=True)
        try:
            if server.stdout.readline().strip() != '0':
                raise ValueError('delivered-clock controller failed to reset')
            count = 0
            with NgspiceSession(args.ngspice, work, work / 'interactive.log') as spice:
                spice.command('\n'.join(setup) + '\nsource live.cir\nstop when time = 1e-15\n' + start,
                              timeout=1200)
                clock = NgspiceVoltageClock(spice, metadata['clock'], .99, 2.31)
                if clock.high_state:
                    raise ValueError('delivered clock already high at initialization')
                while not clock.ended:
                    event = clock.advance(metadata['stop_s'], timeout=1200)
                    if event['edge'] == 'rising':
                        server.stdin.write('1\n')
                        server.stdin.flush()
                        count += 1
                        if server.stdout.readline().strip() != str(count):
                            raise ValueError('controller failed to advance on the delivered clock')
                spice.command('\n'.join(output))
            server.stdin.close()
            if server.wait(timeout=10):
                raise ValueError('delivered-clock controller server failed')
        finally:
            if server.poll() is None:
                server.kill()
                server.wait()
            if not server.stdin.closed:
                server.stdin.close()
            server.stdout.close()
    (work / 'clock_controller.json').write_text(json.dumps(dict(count=count)) + '\n')


def run(args):
    work = args.work.resolve()
    work.mkdir(parents=True, exist_ok=True)
    report_path = work / 'checks.json'
    report_path.unlink(missing_ok=True)
    (work.parent / 'comparison.json').unlink(missing_ok=True)
    hashes = source_dependencies([SOURCE.with_suffix('.cpp')], ANALOG.parent, args.cxx,
        ['-std=c++20', '-I' + str(ANALOG), '-I' + str(ANALOG / 'include'), '-I/usr/include/eigen3'])
    hashes.update({str(p): digest(p) for p in (SOURCE, ANALOG / 'VerilogA/compare.py', ANALOG / 'VerilogA/live.py')})
    binaries = {str(args.bench.resolve()): digest(args.bench.resolve())}
    if args.flow == 'veriloga':
        binaries.update({str(p.resolve()): digest(p.resolve()) for p in (args.openvaf, args.ngspice)})
    report = dict(status='running', flow=args.flow, cases={}, source_sha256=hashes, binary_sha256=binaries,
                  scope='connected generic MOS reference PLL and voltage interface; no IHP PLL, recovered-data loop or routed clock tree')
    try:
        for name, (load, refined) in CASES.items():
            case = work / name
            case.mkdir(exist_ok=True)
            with (case / 'bench.log').open('w') as log:
                execute([str(args.bench.resolve()), str(case), 'run' if args.flow == 'cpp' else 'export',
                         '0', str(load), str(int(refined))], stdout=log, stderr=-2, check=True, timeout=1200)
            if args.flow == 'veriloga':
                with (case / 'compile.log').open('w') as log:
                    execute([str(args.openvaf.resolve()), 'design.va', '-o', 'design.osdi'], cwd=case,
                            stdout=log, stderr=-2, check=True, timeout=180)
                live(args, case)
            trace = case / ('cpp/trace.csv' if args.flow == 'cpp' else 'spice.dat')
            rows = read(trace, args.flow)
            result = contract(rows, load, refined, json.loads((case / 'clock_controller.json').read_text()))
            compressed = trace.with_suffix(trace.suffix + '.gz')
            with trace.open('rb') as source, gzip.open(compressed, 'wb', compresslevel=6) as destination:
                shutil.copyfileobj(source, destination)
            if read(compressed, args.flow) != rows:
                raise ValueError('clock-domain archive is not lossless')
            trace.unlink()
            result.update(waveform=str(compressed), waveform_sha256=digest(compressed),
                          controller_sha256=digest(case / 'clock_controller.json'))
            report['cases'][name] = result
            report_path.write_text(json.dumps(report, indent=2) + '\n')
            print(name + ': pass', flush=True)
        a, b = [report['cases'][name] for name in ('nominal', 'refined')]
        if len(a['prefix_edges_s']) != len(b['prefix_edges_s']):
            raise ValueError('connected clock edge count changed under refinement')
        timing = max(abs(x - y) for x, y in zip(a['prefix_edges_s'], b['prefix_edges_s']))
        voltage = max(abs(x - y) for x, y in zip(a['prefix_control_V'], b['prefix_control_V']))
        if timing > 80e-12 or voltage > .005:
            raise ValueError('connected clock did not converge under time-step refinement')
        report['refinement'] = dict(maximum_edge_difference_s=timing, maximum_control_difference_V=voltage)
        if any(digest(Path(path)) != expected for path, expected in (hashes | binaries).items()):
            raise ValueError('clock-domain input changed during validation')
        report['status'] = 'pass'
    except BaseException as error:
        report.update(status='failed', error=str(error))
        raise
    finally:
        report_path.write_text(json.dumps(report, indent=2) + '\n')


def compare(args):
    output = args.work / 'comparison.json'
    output.unlink(missing_ok=True)
    reports = []
    for flow in ('cpp', 'veriloga'):
        root = args.work / flow
        report = json.loads((root / 'checks.json').read_text())
        if report['status'] != 'pass' or report['flow'] != flow or set(report['cases']) != set(CASES):
            raise ValueError('both connected-clock flows must pass independently')
        for path, expected in (report['source_sha256'] | report['binary_sha256']).items():
            if digest(Path(path)) != expected:
                raise ValueError('stale connected-clock input')
        for name, (load, refined) in CASES.items():
            old = report['cases'][name]
            controller = root / name / 'clock_controller.json'
            if digest(Path(old['waveform'])) != old['waveform_sha256'] or digest(controller) != old['controller_sha256']:
                raise ValueError('connected-clock evidence changed')
            current = contract(read(Path(old['waveform']), flow), load, refined, json.loads(controller.read_text()))
            if any(old[key] != value for key, value in current.items()):
                raise ValueError('connected-clock absolute checks differ on repetition')
        reports.append(report)
    if reports[0]['source_sha256'] != reports[1]['source_sha256']:
        raise ValueError('connected-clock flows used different source')
    result = {}
    for name in CASES:
        a, b = [report['cases'][name] for report in reports]
        first, second = a['prefix_edges_s'], b['prefix_edges_s']
        if len(first) != len(second):
            raise ValueError('connected-clock startup edges disagree across solvers')
        timing = max(abs(x - y) for x, y in zip(first, second))
        voltage = max(abs(x - y) for x, y in zip(a['prefix_control_V'], b['prefix_control_V']))
        if timing > 80e-12 or voltage > .005:
            raise ValueError('connected-clock solvers disagree')
        result[name] = dict(maximum_edge_difference_s=timing, maximum_control_difference_V=voltage)
    output.write_text(json.dumps(dict(status='pass', cases=result), indent=2) + '\n')


if __name__ == '__main__':
    configure_cleanup()
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--flow', choices=('cpp', 'veriloga', 'compare'), required=True)
    parser.add_argument('--work', type=Path, required=True)
    parser.add_argument('--bench', type=Path)
    parser.add_argument('--cxx', default='c++')
    parser.add_argument('--openvaf', type=Path)
    parser.add_argument('--ngspice', type=Path)
    args = parser.parse_args()
    compare(args) if args.flow == 'compare' else run(args)
