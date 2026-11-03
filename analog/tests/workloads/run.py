"""Short native-first solver checks; ngspice is an independent reference only."""
import argparse
import bisect
import csv
import hashlib
import json
import math
import os
from pathlib import Path
import signal
import subprocess
import tempfile
import time

CASES = ('linear_dc', 'nonlinear_dc', 'linear_charge', 'nonlinear_charge', 'coupled_rc', 'pwl_breakpoints')
MAX_POINTS = 10000
PHASE_SECONDS = 20
SUITE_SECONDS = 180
VOLTAGE_TOLERANCE = 3e-5


def digest(path):
    return hashlib.sha256(Path(path).read_bytes()).hexdigest()


def execute(command, directory, label, deadline):
    remaining = min(PHASE_SECONDS, deadline - time.monotonic())
    if remaining <= 0:
        raise TimeoutError('smoke suite exceeded its wall-time budget')
    start = time.monotonic()
    # These small circuits run without an hours-long EDA queue. Both engines
    # have bounded CPU, address space, output size and total process-tree life.
    command = ['prlimit', '--as=536870912', '--cpu=20', '--fsize=16777216', '--', *map(str, command)]
    with (directory / (label + '.log')).open('w') as log:
        process = subprocess.Popen(command, cwd=directory, stdout=log, stderr=subprocess.STDOUT,
                                   start_new_session=True, env=dict(os.environ, OMP_NUM_THREADS='1',
                                       OMP_THREAD_LIMIT='1', OMP_MAX_ACTIVE_LEVELS='1',
                                       OPENBLAS_NUM_THREADS='1', MKL_NUM_THREADS='1', BLIS_NUM_THREADS='1',
                                       VECLIB_MAXIMUM_THREADS='1', NUMEXPR_NUM_THREADS='1'))
        try:
            result = process.wait(timeout=remaining)
        except BaseException:
            try:
                os.killpg(process.pid, signal.SIGKILL)
            except ProcessLookupError:
                pass
            process.wait()
            raise
    if result:
        raise ValueError(f'{label} failed ({result}); inspect {directory / (label + ".log")}')
    return time.monotonic() - start


def read_trace(path, reference=False, dc=False):
    if path.stat().st_size > 8 * 1024 * 1024:
        raise ValueError('smoke trace exceeded the byte budget')
    with path.open() as stream:
        reader = (line.split() for line in stream) if reference else csv.reader(stream)
        header = next(reader)
        if not 2 <= len(header) <= 3:
            raise ValueError('invalid observation columns')
        expected = (['v(out)', 'v(other)'] if reference else ['out', 'other'])[:len(header) - 1]
        if header[1:] != expected or (not reference and header[0] != 'time_s'):
            raise ValueError('unexpected observation names or order')
        rows = []
        for line in reader:
            if not line:
                continue
            values = list(map(float, line))
            if len(values) != len(header) or not all(math.isfinite(v) for v in values):
                raise ValueError('invalid waveform sample')
            if dc:
                values[0] = 0  # .op scale is not simulated time.
            if rows and values[0] <= rows[-1][0]:
                raise ValueError('non-increasing waveform time')
            rows.append(values)
            if len(rows) > MAX_POINTS:
                raise ValueError('smoke trace exceeded the point budget')
    if not rows or (dc and len(rows) != 1):
        raise ValueError('missing or malformed operating point')
    return rows


def check(case, rows, input_value):
    dc = case.endswith('_dc')
    if not dc:
        if len(rows) < 1000 or not 0 <= rows[0][0] <= 1e-9 or abs(rows[-1][0] - 2e-6) > 1e-15:
            raise ValueError('incomplete transient window')
        if max(b[0] - a[0] for a, b in zip(rows, rows[1:])) > 1.01e-9:
            raise ValueError('insufficient transient resolution')
    worst = 0
    integral = 0
    previous = rows[0]
    for row in rows:
        t, v = row[:2]
        if case == 'linear_dc':
            error = abs(v - input_value / 2)
        elif case == 'nonlinear_dc':
            error = abs(v**3 + 2 * v - input_value)
        elif case == 'linear_charge':
            error = abs(v - math.exp(-t / 1e-6))
        elif case == 'nonlinear_charge':
            integral += (previous[1] + v) * .5 * (t - previous[0]) / 1e-6
            supplied = (t*t / (2e-7) if t <= 1e-7 else t - .5e-7) / 1e-6
            # Integrate independently known source current and resistor loss.
            # Charge is C*(V+V^3), not C(V)*V.
            error = abs(v + v**3 + integral - supplied)
        elif case == 'coupled_rc':
            common, difference = math.exp(-t / 1e-6), math.exp(-t / 2e-6)
            if len(row) != 3:
                raise ValueError('missing coupled node')
            error = max(abs(v - (common + difference) / 2), abs(row[2] - (common - difference) / 2))
        elif case == 'pwl_breakpoints':
            def ramp(delay):
                x = max(0, t - delay)
                return x - .5e-6 * (-math.expm1(-x / .5e-6))
            expected = .5 / .2e-6 * (ramp(0) - ramp(.2e-6) - ramp(.8e-6) + ramp(1e-6))
            error = abs(v - expected)
        else:
            raise ValueError('unknown circuit contract')
        worst = max(worst, error)
        previous = row
    if worst > 2e-5:
        raise ValueError(f'{case}: independent circuit contract error {worst}')
    return dict(status='pass', maximum_contract_error=worst, points=len(rows))


def compare(first, second):
    if len(first[0]) != len(second[0]):
        raise ValueError('different observation sets')
    if len(first) == len(second) == 1:
        error = max(abs(a - b) for a, b in zip(first[0][1:], second[0][1:]))
    else:
        # Compare at EVERY recorded time from BOTH runs. No decimation and no
        # extrapolation; ngspice UIC may start just after native time zero.
        start, stop = max(first[0][0], second[0][0]), min(first[-1][0], second[-1][0])
        error = 0
        for sampled, interpolated in ((first, second), (second, first)):
            times = [row[0] for row in interpolated]
            for row in sampled:
                if not start <= row[0] <= stop:
                    continue
                index = min(max(1, bisect.bisect_left(times, row[0])), len(times) - 1)
                a, b = interpolated[index - 1:index + 1]
                fraction = (row[0] - a[0]) / (b[0] - a[0])
                for col in range(1, len(row)):
                    error = max(error, abs(row[col] - a[col] - fraction * (b[col] - a[col])))
    if error > VOLTAGE_TOLERANCE:
        raise ValueError(f'waveform difference {error} V exceeds {VOLTAGE_TOLERANCE} V')
    return dict(status='pass', maximum_error_V=error, tolerance_V=VOLTAGE_TOLERANCE)


def run(args):
    args.work.mkdir(parents=True, exist_ok=True)
    work = Path(tempfile.mkdtemp(prefix='smoke-', dir=args.work.resolve()))
    inputs = [args.bench, Path(__file__), Path(__file__).with_name('bench.cpp')]
    if args.ngspice:
        inputs.append(args.ngspice)
    report = dict(status='running', profile='smoke', primary_engine='cpphdl',
                  reference_engine='ngspice' if args.ngspice else None,
                  work=str(work), cases=[], phase_timeout_s=PHASE_SECONDS, suite_timeout_s=SUITE_SECONDS,
                  process_models_qualified=False, full_chip_qualified=False,
                  input_sha256={str(p.resolve()): digest(p) for p in inputs})
    deadline = time.monotonic() + SUITE_SECONDS
    try:
        for case in CASES:
            for input_value in ((-1, -.5, 0, .5, 1) if case.endswith('_dc') else (1,)):
                directory = work / f'{case}_{input_value}'
                directory.mkdir()
                row = dict(case=case, input=input_value)
                report['cases'].append(row)
                native = []
                for refinement in (1, 2):
                    output = directory / str(refinement)
                    output.mkdir()
                    elapsed = execute([args.bench.resolve(), case, output, refinement, input_value],
                                      output, 'cpphdl', deadline)
                    values = read_trace(output / 'cpp.csv', dc=case.endswith('_dc'))
                    contract = check(case, values, input_value)
                    row[f'cpphdl_{refinement}'] = dict(**contract, process_wall_s=elapsed,
                        statistics=json.loads((output / 'native.json').read_text()),
                        trace_sha256=digest(output / 'cpp.csv'))
                    native.append(values)
                row['native_refinement'] = compare(*native)
                if not args.ngspice:
                    continue
                # A native failure stops here; no reference invocation can
                # substitute its result for the primary simulation.
                output = directory / '2'
                elapsed = execute([args.ngspice.resolve(), '-n', '-b', 'reference.cir'],
                                  output, 'ngspice', deadline)
                reference = read_trace(output / 'reference.dat', reference=True, dc=case.endswith('_dc'))
                row['ngspice'] = dict(**check(case, reference, input_value), process_wall_s=elapsed,
                                      trace_sha256=digest(output / 'reference.dat'))
                row['comparison'] = compare(native[-1], reference)
                row['deck_sha256'] = digest(output / 'reference.cir')
        if any(digest(path) != sha for path, sha in report['input_sha256'].items()):
            raise ValueError('workload input changed during simulation')
        report['status'] = 'pass'
    except BaseException as error:
        report.update(status='fail', error=str(error))
        raise
    finally:
        report['suite_wall_s'] = SUITE_SECONDS - (deadline - time.monotonic())
        data = json.dumps(report, indent=2) + '\n'
        (work / 'checks.json').write_text(data)
        (args.work / 'checks.json').write_text(data)
    print(f'PASS {len(report["cases"])} short cases: C++RTL and refinement' +
          ('; independent ngspice reference and shared contracts' if args.ngspice else ''))


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    for name in ('bench', 'work'):
        parser.add_argument('--' + name, required=True, type=Path)
    parser.add_argument('--ngspice', type=Path, help='optional independent reference; never the primary simulator')
    run(parser.parse_args())
