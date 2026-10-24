"""Independent storage/clear/division truth, then electrical solver comparison."""
import argparse
import bisect
import hashlib
import json
import math
from pathlib import Path
import shlex
import subprocess
import sys

SOURCE = Path(__file__).resolve()
ANALOG = SOURCE.parents[1]
sys.path.insert(0, str(ANALOG / 'VerilogA'))
from compare import read_cpp, read_spice


def digest(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


def oracle(kind):
    # A behavior oracle describes input transitions and edge-triggered truth;
    # it contains no NAND network, MOS law or analog integration.
    events = [(12.05, 'clear', False), (139.05, 'clear', True), (145.05, 'clear', False)]
    events += [(2 * half + .05, 'clock', bool(half & 1)) for half in range(1, 192)]
    events += [(2 * index + .85, 'data', (index * 73 + index // 3) % 17 < 8) for index in range(192)]
    clear, clock, data, q = True, False, False, False
    states, edges = [(0., False)], []
    for time, signal, value in sorted(events):
        was = q
        rising = signal == 'clock' and value
        if signal == 'clear':
            clear = value
        elif signal == 'clock':
            clock = value
        else:
            data = value
        if clear:
            q = False
        elif kind == 'latch' and clock:
            q = data
        elif kind != 'latch' and rising:
            q = not q if kind == 'divider' else data
        if q != was:
            states.append((time * 1e-9, q))
            if time >= 16:
                edges.append((time * 1e-9, int(q)))
    return states, edges


def read(directory, flow):
    header = ['time_s', 'Q', 'Q_N', 'D', 'CLOCK', 'CLEAR', 'VDD']
    if flow == 'cpp':
        actual, rows = read_cpp(directory / 'cpp/trace.csv')
        if actual != header:
            raise ValueError('storage observation interface changed')
        return rows
    return read_spice(directory / 'spice.dat', header)


def contract(rows, kind):
    if (not rows or abs(rows[-1][0] - 384e-9) > 1e-14
            or any(not math.isfinite(x) for row in rows for x in row)):
        raise ValueError('incomplete/nonfinite storage waveform')
    times = [row[0] for row in rows]
    if any(b <= a or b - a > 31.251e-12 for a, b in zip(times, times[1:])):
        raise ValueError('insufficient storage waveform resolution')

    def at(time, column):
        i = min(max(1, bisect.bisect_left(times, time)), len(times) - 1)
        a, b = rows[i - 1], rows[i]
        return a[column] + (b[column] - a[column]) * (time - a[0]) / (b[0] - a[0])

    states, expected_edges = oracle(kind)
    state_times = [entry[0] for entry in states]
    probes = []
    for tick in range(128, 3072):
        time = tick * .125e-9
        index = bisect.bisect_right(state_times, time) - 1
        if time - state_times[index] < .5e-9:
            continue
        expected = states[index][1]
        values = [at(time, column) for column in (1, 2, 6)]
        for value, bit in zip(values[:2], (expected, not expected)):
            if not (value > .95 if bit else value < .2):
                raise ValueError(f'{kind} truth/hold/clear failed at {time / 1e-9:.5f} ns: {values}')
        if not 1.0 < values[2] <= 1.201:
            raise ValueError('storage supply contract failed')
        probes.append(values)
    edges = []
    for a, b in zip(rows, rows[1:]):
        if a[0] < 16e-9:
            continue
        if (a[1] >= .6) != (b[1] >= .6):
            edges.append([a[0] + (.6 - a[1]) * (b[0] - a[0]) / (b[1] - a[1]), int(b[1] > a[1])])
    if len(edges) != len(expected_edges):
        raise ValueError(f'{kind} edge count differs: {len(edges)} != {len(expected_edges)}')
    for (actual, direction), (expected, wanted) in zip(edges, expected_edges):
        if direction != wanted or not expected - .1e-9 <= actual <= expected + .5e-9:
            raise ValueError(f'{kind} wrong edge or propagation delay: {actual}, expected {expected}')
    return dict(edges=edges, plateau_probes_V=probes, checked_states=len(probes),
                checks=dict(truth_and_hold=True, asynchronous_clear=True, edge_order_and_count=True,
                            finite_supply=True))


def run(args):
    work = args.work.resolve()
    work.mkdir(parents=True, exist_ok=True)
    (work / 'checks.json').unlink(missing_ok=True)
    listing = subprocess.check_output([args.cxx, '-std=c++20', '-MM', '-MT', 'deps',
        '-I' + str(ANALOG), '-I' + str(ANALOG / 'include'), '-I' + str(ANALOG.parent / 'include'),
        str(SOURCE.with_suffix('.cpp'))], text=True)
    dependencies = [Path(p).resolve() for p in shlex.split(listing.replace('\\\n', ' ').split(':', 1)[1])]
    hashes = {str(path): digest(path) for path in dependencies if path.is_relative_to(ANALOG.parent)}
    hashes[str(SOURCE)] = digest(SOURCE)
    binaries = {str(args.bench.resolve()): digest(args.bench.resolve())}
    if args.flow == 'veriloga':
        binaries.update({str(p.resolve()): digest(p.resolve()) for p in (args.openvaf, args.ngspice)})
    result = dict(status='running', flow=args.flow, source_sha256=hashes, binary_sha256=binaries, cases={})
    for kind in ('latch', 'dff', 'divider'):
        for loaded in (0, 1):
            name = kind + '_' + str(loaded)
            directory = work / name
            directory.mkdir(exist_ok=True)
            commands = [('bench', [args.bench.resolve(), directory, 'run' if args.flow == 'cpp' else 'export', kind, loaded])]
            if args.flow == 'veriloga':
                commands += [('compile', [args.openvaf.resolve(), 'design.va', '-o', 'design.osdi']),
                             ('ngspice', [args.ngspice.resolve(), '-n', '-b', 'bench.cir'])]
            for label, command in commands:
                with (directory / (label + '.log')).open('w') as log:
                    subprocess.run(list(map(str, command)), cwd=directory, stdout=log, stderr=-2, check=True, timeout=300)
            checked = contract(read(directory, args.flow), kind)
            waveform = directory / ('cpp/trace.csv' if args.flow == 'cpp' else 'spice.dat')
            checked['waveform_sha256'] = digest(waveform)
            result['cases'][name] = checked
            print(name + ': pass', flush=True)
    if any(digest(Path(p)) != value for p, value in (hashes | binaries).items()):
        raise ValueError('storage validation inputs changed during run')
    result['status'] = 'pass'
    (work / 'checks.json').write_text(json.dumps(result, indent=2) + '\n')


def compare(args):
    (args.work / 'comparison.json').unlink(missing_ok=True)
    reports = []
    for flow in ('cpp', 'veriloga'):
        work = args.work.resolve() / flow
        result = json.loads((work / 'checks.json').read_text())
        if result.get('status') != 'pass' or result.get('flow') != flow or len(result['cases']) != 6:
            raise ValueError('both independent storage flows must pass first')
        if any(digest(Path(p)) != value for p, value in (result['source_sha256'] | result['binary_sha256']).items()):
            raise ValueError('stale storage validation input')
        for name, old in result['cases'].items():
            directory = work / name
            waveform = directory / ('cpp/trace.csv' if flow == 'cpp' else 'spice.dat')
            if digest(waveform) != old['waveform_sha256']:
                raise ValueError('storage waveform changed')
            checked = contract(read(directory, flow), name.rsplit('_', 1)[0])
            if any(old[key] != value for key, value in checked.items()):
                raise ValueError('storage absolute check changed')
        reports.append(result)
    if reports[0]['source_sha256'] != reports[1]['source_sha256']:
        raise ValueError('storage flows have different sources')
    cases = {}
    for name, a in reports[0]['cases'].items():
        b = reports[1]['cases'][name]
        if len(a['edges']) != len(b['edges']) or any(x[1] != y[1] for x, y in zip(a['edges'], b['edges'])):
            raise ValueError('storage solver edge sequence differs')
        timing = max(abs(x[0] - y[0]) for x, y in zip(a['edges'], b['edges']))
        delta = [x - y for first, second in zip(a['plateau_probes_V'], b['plateau_probes_V']) for x, y in zip(first, second)]
        rms = math.sqrt(sum(x * x for x in delta) / len(delta))
        if timing > .1e-9 or rms > .02:
            raise ValueError('storage solvers disagree: ' + name)
        cases[name] = dict(plateau_rms_difference_V=rms, maximum_edge_difference_s=timing)
    (args.work / 'comparison.json').write_text(json.dumps(dict(status='pass', cases=cases), indent=2) + '\n')


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--flow', choices=('cpp', 'veriloga', 'compare'), required=True)
    parser.add_argument('--work', type=Path, required=True)
    parser.add_argument('--bench', type=Path)
    parser.add_argument('--openvaf', type=Path)
    parser.add_argument('--ngspice', type=Path)
    parser.add_argument('--cxx', default='/usr/bin/c++')
    args = parser.parse_args()
    compare(args) if args.flow == 'compare' else run(args)
