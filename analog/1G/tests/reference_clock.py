"""Qualify the physical reference PLL in each solver, then compare results."""
import argparse
import bisect
import csv
import gzip
import hashlib
import json
import math
from pathlib import Path
import shutil
import statistics
import subprocess
import sys

SOURCE = Path(__file__).resolve()
ANALOG = SOURCE.parents[2]
sys.path.insert(0, str(ANALOG / 'VerilogA'))
from compare import checked_rows
from hardware import configure_cleanup, execute, source_dependencies
from live import NgspiceSession, NgspiceVoltageClock

CASES = dict(nominal=(0, 0, 0), slow_reference=(-100, 0, 0), fast_reference=(100, 0, 0),
             loaded=(0, 1, 0), overloaded=(0, 2, 0), refined=(0, 0, 1))
HEADER = ['time_s', 'OUT', 'DIVIDED', 'REF', 'CONTROL', 'VDD', 'UP', 'DOWN']


def digest(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


def waveform(directory, flow):
    raw = directory / ('cpp/trace.csv' if flow == 'cpp' else 'spice.dat')
    return raw if raw.exists() else raw.with_suffix(raw.suffix + '.gz')


def read(path, flow):
    with (gzip.open(path, 'rt') if path.suffix == '.gz' else path.open()) as file:
        if flow == 'cpp':
            reader = csv.reader(file)
            if next(reader) != HEADER:
                raise ValueError('reference PLL observation interface changed')
        else:
            names = next(file).split()
            if len(names) != len(HEADER) or names[0] != 'time':
                raise ValueError('reference PLL SPICE observation interface changed')
            reader = (line.split() for line in file if line.strip())
        return checked_rows(reader, len(HEADER), str(path))


def contract(rows, ppm, load, refined, controller):
    stop, step = (480e-9, 5e-12) if refined else (960e-9, 10e-12)
    times = [row[0] for row in rows]
    if not rows or abs(times[-1] - stop) > 1e-14 or max(b - a for a, b in zip(times, times[1:])) > step * 1.001:
        raise ValueError('incomplete or insufficiently resolved reference PLL trajectory')
    if rows[0][1] > .36:
        raise ValueError('reference PLL clock did not start at its declared low state')
    high, count = False, 0
    for row in rows:
        if not high and row[1] >= .84:
            high = True
            count += 1
        elif high and row[1] <= .36:
            high = False
    if type(controller.get('count')) is not int or controller['count'] != count:
        raise ValueError('C++RTL controller did not advance once per achieved electrical clock edge')

    def at(time, column):
        i = min(max(1, bisect.bisect_left(times, time)), len(times) - 1)
        a, b = rows[i - 1], rows[i]
        return a[column] + (b[column] - a[column]) * (time - a[0]) / (b[0] - a[0])

    def edges(column, start, end):
        result = []
        for a, b in zip(rows, rows[1:]):
            if a[column] < .6 <= b[column]:
                time = a[0] + (.6 - a[column]) * (b[0] - a[0]) / (b[column] - a[column])
                if start < time < end:
                    result.append(time)
        return result

    prefix_edges = edges(1, 100e-9, 470e-9)
    prefix_control = [at(ns * 1e-9, 4) for ns in range(16, 480)]
    steady = [row for row in rows if row[0] > (100e-9 if refined else 800e-9)]
    if min(row[5] for row in steady) < 1.1 or max(row[5] for row in steady) > 1.201:
        raise ValueError('reference PLL supply collapsed')
    if any(not -.05 < row[c] < 1.25 for row in steady for c in (1, 2, 4, 6, 7)):
        raise ValueError('reference PLL has invalid rail excursions')
    result = dict(prefix_output_edges_s=prefix_edges, prefix_control_V=prefix_control,
                  controller_count=count,
                  checks=dict(finite_complete=True, supply_and_rails=True, achieved_clock_controller=True))
    if refined:
        if len(prefix_edges) < 20:
            raise ValueError('refined cold-start PLL did not begin oscillating')
        return result
    output = edges(1, 800e-9, 959.9e-9)
    divided = edges(2, 800e-9, 959.9e-9)
    reference = edges(3, 790e-9, 960e-9)
    if len(output) < 4 or len(divided) < 2:
        raise ValueError('PLL oscillator or divider stopped')
    periods = [b - a for a, b in zip(output, output[1:])]
    frequency = 1 / statistics.mean(periods)
    divided_frequency = (len(divided) - 1) / (divided[-1] - divided[0])
    controls = [row[4] for row in steady]
    result.update(frequency_Hz=frequency, divided_frequency_Hz=divided_frequency,
                  mean_control_V=statistics.mean(controls), control_ripple_V=max(controls) - min(controls),
                  peak_to_peak_period_s=max(periods) - min(periods))
    if load == 2:
        # A forty-times-larger stage load must defeat the intended rate. An
        # ideal imposed clock would incorrectly pass the normal lock contract.
        if frequency >= 200e6 or statistics.mean(controls) < .9:
            raise ValueError('overloaded PLL did not expose its physical tuning limit')
        result['expected_loss_of_lock'] = True
        return result
    target = 250e6 * (1 + ppm * 1e-6)
    phases = [t - min(reference, key=lambda ref: abs(ref - t)) for t in divided]
    if (abs(frequency / target - 1) > 500e-6 or abs(divided_frequency / (target / 2) - 1) > 500e-6
            or abs(frequency / (2 * divided_frequency) - 1) > 100e-6):
        raise ValueError('reference PLL has not acquired the fundamental 2:1 rate')
    if max(abs(value) for value in phases) > 80e-12 or result['peak_to_peak_period_s'] > 100e-12:
        raise ValueError('reference PLL phase/period have not settled')
    if not .35 < min(controls) <= max(controls) < 1.0 or result['control_ripple_V'] > .002:
        raise ValueError('reference PLL tuning voltage is unqualified')
    if min(row[1] for row in steady) > .2 or max(row[1] for row in steady) < 1.0:
        raise ValueError('reference clock lacks valid logic swing')
    result.update(divided_phase_errors_s=phases, output_edges_s=output,
                  expected_loss_of_lock=False)
    result['checks'].update(fundamental_rate=True, stable_phase=True, control_headroom=True)
    return result


def refinement(cases):
    first, second = [cases[name] for name in ('nominal', 'refined')]
    a, b = first['prefix_output_edges_s'], second['prefix_output_edges_s']
    if len(a) != len(b):
        raise ValueError('PLL cold-start edge count changes under refinement')
    timing = max(abs(x - y) for x, y in zip(a, b))
    delta = [x - y for x, y in zip(first['prefix_control_V'], second['prefix_control_V'])]
    rms = math.sqrt(sum(x * x for x in delta) / len(delta))
    if timing > 80e-12 or rms > .003:
        raise ValueError('PLL cold-start trajectory did not converge under refinement')
    if cases['loaded']['mean_control_V'] < first['mean_control_V'] + .005:
        raise ValueError('external clock loading did not change the physical tuning demand')
    return dict(prefix_maximum_edge_difference_s=timing, prefix_control_rms_V=rms)


def live_flow(args, work):
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
                raise ValueError('clock controller did not reset to zero')
            count = 0
            with (work / 'controller.csv').open('w') as events:
                events.write('time_s,count\n')
                with NgspiceSession(args.ngspice, work, work / 'interactive.log') as spice:
                    spice.command('\n'.join(setup) + '\nsource live.cir\nstop when time = 1e-15\n' + start,
                                  timeout=1200)
                    clock = NgspiceVoltageClock(spice, metadata['clock'], .36, .84)
                    if clock.high_state:
                        raise ValueError('live PLL clock is already high at startup')
                    while not clock.ended:
                        event = clock.advance(metadata['stop_s'], timeout=1200)
                        if event['edge'] == 'rising':
                            server.stdin.write('1\n')
                            server.stdin.flush()
                            count += 1
                            if server.stdout.readline().strip() != str(count):
                                raise ValueError('C++RTL clock controller state disagrees with achieved rising edges')
                            events.write(f'{event["time_s"]:.17g},{count}\n')
                    spice.command('\n'.join(output))
            server.stdin.close()
            if server.wait(timeout=10) != 0:
                raise ValueError('clock controller server failed')
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
    (work / 'checks.json').unlink(missing_ok=True)
    hashes = source_dependencies([SOURCE.with_suffix('.cpp')], ANALOG.parent, args.cxx,
        ['-std=c++20', '-I' + str(ANALOG), '-I' + str(ANALOG / 'include'), '-I/usr/include/eigen3'])
    hashes.update({str(p): digest(p) for p in (SOURCE, ANALOG / 'VerilogA/compare.py', ANALOG / 'VerilogA/live.py')})
    binaries = {str(args.bench.resolve()): digest(args.bench.resolve())}
    if args.flow == 'veriloga':
        binaries.update({str(p.resolve()): digest(p.resolve()) for p in (args.openvaf, args.ngspice)})
    report = dict(status='running', flow=args.flow, source_sha256=hashes, binary_sha256=binaries, cases={},
                  scope='generic MOS reference PLL; no recovered-data loop, IHP fit or routed clock qualification')
    for name, parameters in CASES.items():
        case = work / name
        case.mkdir(exist_ok=True)
        commands = [('bench', [args.bench.resolve(), case, 'run' if args.flow == 'cpp' else 'export', *parameters])]
        if args.flow == 'veriloga':
            commands += [('compile', [args.openvaf.resolve(), 'design.va', '-o', 'design.osdi'])]
        for label, command in commands:
            with (case / (label + '.log')).open('w') as log:
                execute(list(map(str, command)), cwd=case, stdout=log, stderr=-2, check=True, timeout=1200)
        if args.flow == 'veriloga':
            live_flow(args, case)
        raw = waveform(case, args.flow)
        controller_path = case / 'clock_controller.json'
        result = contract(read(raw, args.flow), *parameters, json.loads(controller_path.read_text()))
        original = digest(raw)
        archive = raw.with_suffix(raw.suffix + '.gz')
        with raw.open('rb') as source, gzip.open(archive, 'wb', compresslevel=1) as output:
            shutil.copyfileobj(source, output)
        checksum = hashlib.sha256()
        with gzip.open(archive, 'rb') as source:
            for chunk in iter(lambda: source.read(1024 * 1024), b''):
                checksum.update(chunk)
        if checksum.hexdigest() != original:
            raise ValueError('PLL waveform archive differs')
        raw.unlink()
        result.update(waveform_sha256=digest(archive), uncompressed_sha256=original,
                      controller_sha256=digest(controller_path))
        report['cases'][name] = result
        print(name + ': pass', flush=True)
    report['refinement'] = refinement(report['cases'])
    if any(digest(Path(p)) != value for p, value in (hashes | binaries).items()):
        raise ValueError('reference PLL inputs changed during run')
    report['status'] = 'pass'
    (work / 'checks.json').write_text(json.dumps(report, indent=2) + '\n')


def compare(args):
    (args.work / 'comparison.json').unlink(missing_ok=True)
    reports = []
    for flow in ('cpp', 'veriloga'):
        work = args.work.resolve() / flow
        report = json.loads((work / 'checks.json').read_text())
        if report.get('status') != 'pass' or report.get('flow') != flow or set(report['cases']) != set(CASES):
            raise ValueError('both reference PLL flows must pass independently')
        if any(digest(Path(p)) != value for p, value in (report['source_sha256'] | report['binary_sha256']).items()):
            raise ValueError('stale reference PLL validation input')
        for name, parameters in CASES.items():
            old = report['cases'][name]
            path = waveform(work / name, flow)
            if digest(path) != old['waveform_sha256']:
                raise ValueError('reference PLL waveform changed')
            controller_path = work / name / 'clock_controller.json'
            if digest(controller_path) != old['controller_sha256']:
                raise ValueError('reference PLL controller result changed')
            current = contract(read(path, flow), *parameters, json.loads(controller_path.read_text()))
            if any(old[key] != value for key, value in current.items()):
                raise ValueError('reference PLL absolute check changed')
        if refinement(report['cases']) != report['refinement']:
            raise ValueError('reference PLL refinement check changed')
        reports.append(report)
    if reports[0]['source_sha256'] != reports[1]['source_sha256']:
        raise ValueError('reference PLL flows tested different sources')
    cases = {}
    for name in CASES:
        a, b = [report['cases'][name] for report in reports]
        if name in ('overloaded', 'refined'):
            continue
        frequency = abs(a['frequency_Hz'] / b['frequency_Hz'] - 1)
        control = abs(a['mean_control_V'] - b['mean_control_V'])
        edges = a['output_edges_s'], b['output_edges_s']
        if len(edges[0]) != len(edges[1]):
            raise ValueError('steady reference PLL edge counts differ')
        phase = max(abs(x - y) for x, y in zip(*edges))
        if frequency > 200e-6 or control > .003 or phase > 25e-12:
            raise ValueError('independently passing reference PLL solvers disagree: ' + name)
        cases[name] = dict(frequency_difference=frequency, control_difference_V=control,
                          maximum_steady_edge_difference_s=phase)
    (args.work / 'comparison.json').write_text(json.dumps(dict(status='pass', cases=cases), indent=2) + '\n')


if __name__ == '__main__':
    configure_cleanup()
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--flow', choices=('cpp', 'veriloga', 'compare'), required=True)
    parser.add_argument('--work', type=Path, required=True)
    parser.add_argument('--bench', type=Path)
    parser.add_argument('--openvaf', type=Path)
    parser.add_argument('--ngspice', type=Path)
    parser.add_argument('--cxx', default='/usr/bin/c++')
    args = parser.parse_args()
    compare(args) if args.flow == 'compare' else run(args)
