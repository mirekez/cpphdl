"""Check edge order, frequency direction and reset before comparing solvers."""
import argparse
import bisect
import csv
import gzip
import hashlib
import json
import math
from pathlib import Path
import shlex
import shutil
import subprocess
import sys

SOURCE = Path(__file__).resolve()
ANALOG = SOURCE.parents[1]
sys.path.insert(0, str(ANALOG / 'VerilogA'))
from compare import checked_rows

CASES = [(name, 0) for name in ('same', 'ref_leads', 'fb_leads', 'fb_wrapped', 'slow', 'fast', 'missing')]
CASES += [('ref_leads', 1), ('slow', 1)]
HEADER = ['time_s', 'UP', 'DOWN', 'UP_N', 'DOWN_N', 'REF', 'FEEDBACK', 'RESET', 'VDD']


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
                raise ValueError('PFD observation interface changed')
        else:
            names = next(file).split()
            if len(names) != len(HEADER) or names[0] != 'time':
                raise ValueError('PFD SPICE observation interface changed')
            reader = (line.split() for line in file if line.strip())
        return checked_rows(reader, len(HEADER), str(path))


def contract(rows, profile):
    times = [row[0] for row in rows]
    if not rows or abs(times[-1] - 384e-9) > 1e-14 or max(b - a for a, b in zip(times, times[1:])) > 15.626e-12:
        raise ValueError('incomplete or insufficiently resolved PFD trajectory')

    def at(ns, column):
        time = ns * 1e-9
        i = min(max(1, bisect.bisect_left(times, time)), len(times) - 1)
        a, b = rows[i - 1], rows[i]
        return a[column] + (b[column] - a[column]) * (time - a[0]) / (b[0] - a[0])

    def state(ns, up, down):
        actual = [at(ns, c) for c in (1, 2)]
        if any(not (v > .95 if bit else v < .2) for v, bit in zip(actual, (up, down))):
            raise ValueError(f'PFD {profile} wrong state at {ns} ns: {actual}')

    for ns in (138.5, 145, 150):
        state(ns, False, False)
    if profile in ('same', 'ref_leads', 'fb_leads', 'fb_wrapped', 'missing'):
        for base in range(24, 376, 8):
            if 132 <= base <= 152:
                continue
            if profile == 'ref_leads':
                state(base + .5, True, False)
                state(base + 1.8, False, False)
            elif profile == 'fb_wrapped' and base >= 160:
                # Releasing clear during the early feedback pulse causes the
                # next reference edge to pair with the following feedback edge.
                # The physical detector then sees +7 ns rather than -1 ns.
                state(base + .5, True, False)
                state(base + 7.8, False, False)
            elif profile in ('fb_leads', 'fb_wrapped'):
                state(base - .5, False, True)
                state(base + .8, False, False)
            elif profile == 'missing':
                state(base + .8, True, False)
            else:
                state(base + .8, False, False)
    integral = [0.] * 9
    elapsed = 0.
    for a, b in zip(rows, rows[1:]):
        if 200e-9 <= a[0] and b[0] <= 380e-9:
            dt = b[0] - a[0]
            elapsed += dt
            for c in range(1, 9):
                integral[c] += dt * (a[c] + b[c]) / 2
    means = [value / elapsed for value in integral]
    signed = means[1] - means[2]
    valid = (abs(signed) < .01 and max(means[1:3]) < .1 if profile == 'same' else
             .10 < signed < .22 if profile == 'ref_leads' else
             -.22 < signed < -.10 if profile == 'fb_leads' else
             .9 < signed < 1.15 if profile == 'fb_wrapped' else
             signed > .4 if profile == 'slow' else
             signed < -.3 if profile == 'fast' else
             signed > .95 and means[2] < .01)
    if not valid:
        raise ValueError(f'PFD {profile} incorrect frequency/phase direction: {means[1:3]}')
    probes = []
    for ns in (201.75 + 2 * i for i in range(89)):
        values = [at(ns, c) for c in (1, 2, 3, 4, 8)]
        if abs(values[0] + values[2] - values[4]) > .1 or abs(values[1] + values[3] - values[4]) > .1:
            raise ValueError('PFD complementary storage outputs disagree')
        if not 1.0 < values[4] <= 1.201:
            raise ValueError('PFD supply collapsed')
        probes.append(values)
    return dict(mean_up_V=means[1], mean_down_V=means[2], mean_signed_error_V=signed,
                plateau_probes_V=probes,
                checks=dict(edge_order=True, asynchronous_clear=True, frequency_direction=True,
                            complementary_storage=True, supplied_operation=True))


def run(args):
    work = args.work.resolve()
    work.mkdir(parents=True, exist_ok=True)
    (work / 'checks.json').unlink(missing_ok=True)
    listing = subprocess.check_output([args.cxx, '-std=c++20', '-MM', '-MT', 'deps',
        '-I' + str(ANALOG), '-I' + str(ANALOG / 'include'), '-I' + str(ANALOG.parent / 'include'),
        str(SOURCE.with_suffix('.cpp'))], text=True)
    dependencies = [Path(p).resolve() for p in shlex.split(listing.replace('\\\n', ' ').split(':', 1)[1])]
    hashes = {str(p): digest(p) for p in dependencies if p.is_relative_to(ANALOG.parent)}
    hashes.update({str(p): digest(p) for p in (SOURCE, ANALOG / 'VerilogA/compare.py')})
    binaries = {str(args.bench.resolve()): digest(args.bench.resolve())}
    if args.flow == 'veriloga':
        binaries.update({str(p.resolve()): digest(p.resolve()) for p in (args.openvaf, args.ngspice)})
    report = dict(status='running', flow=args.flow, source_sha256=hashes, binary_sha256=binaries, cases={})
    for profile, loaded in CASES:
        name = profile + '_' + str(loaded)
        case = work / name
        case.mkdir(exist_ok=True)
        commands = [('bench', [args.bench.resolve(), case, 'run' if args.flow == 'cpp' else 'export', profile, loaded])]
        if args.flow == 'veriloga':
            commands += [('compile', [args.openvaf.resolve(), 'design.va', '-o', 'design.osdi']),
                         ('ngspice', [args.ngspice.resolve(), '-n', '-b', 'bench.cir'])]
        for label, command in commands:
            with (case / (label + '.log')).open('w') as log:
                subprocess.run(list(map(str, command)), cwd=case, stdout=log, stderr=-2, check=True, timeout=300)
        raw = waveform(case, args.flow)
        result = contract(read(raw, args.flow), profile)
        original = digest(raw)
        archive = raw.with_suffix(raw.suffix + '.gz')
        with raw.open('rb') as source, gzip.open(archive, 'wb', compresslevel=1) as output:
            shutil.copyfileobj(source, output)
        checksum = hashlib.sha256()
        with gzip.open(archive, 'rb') as source:
            for chunk in iter(lambda: source.read(1024 * 1024), b''):
                checksum.update(chunk)
        if checksum.hexdigest() != original:
            raise ValueError('PFD waveform archive differs')
        raw.unlink()
        result.update(waveform_sha256=digest(archive), uncompressed_sha256=original)
        report['cases'][name] = result
        print(name + ': pass', flush=True)
    if any(digest(Path(p)) != value for p, value in (hashes | binaries).items()):
        raise ValueError('PFD inputs changed during validation')
    report['status'] = 'pass'
    (work / 'checks.json').write_text(json.dumps(report, indent=2) + '\n')


def compare(args):
    (args.work / 'comparison.json').unlink(missing_ok=True)
    reports = []
    for flow in ('cpp', 'veriloga'):
        work = args.work.resolve() / flow
        report = json.loads((work / 'checks.json').read_text())
        expected = {profile + '_' + str(loaded) for profile, loaded in CASES}
        if report.get('status') != 'pass' or report.get('flow') != flow or set(report['cases']) != expected:
            raise ValueError('both PFD flows must pass independently')
        if any(digest(Path(p)) != value for p, value in (report['source_sha256'] | report['binary_sha256']).items()):
            raise ValueError('stale PFD source or binary')
        for name, old in report['cases'].items():
            path = waveform(work / name, flow)
            if digest(path) != old['waveform_sha256']:
                raise ValueError('PFD waveform changed')
            current = contract(read(path, flow), name.rsplit('_', 1)[0])
            if any(old[key] != value for key, value in current.items()):
                raise ValueError('PFD absolute check changed')
        reports.append(report)
    if reports[0]['source_sha256'] != reports[1]['source_sha256']:
        raise ValueError('PFD flows tested different sources')
    differences = {}
    for name, first in reports[0]['cases'].items():
        second = reports[1]['cases'][name]
        values = [a - b for x, y in zip(first['plateau_probes_V'], second['plateau_probes_V']) for a, b in zip(x, y)]
        rms = math.sqrt(sum(value * value for value in values) / len(values))
        mean = abs(first['mean_signed_error_V'] - second['mean_signed_error_V'])
        if rms > .02 or mean > .01:
            raise ValueError('independently passing PFD solvers disagree: ' + name)
        differences[name] = dict(plateau_rms_difference_V=rms, mean_signed_error_difference_V=mean)
    (args.work / 'comparison.json').write_text(json.dumps(dict(status='pass', cases=differences), indent=2) + '\n')


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
