#!/usr/bin/env python3
"""Regress full-TX cold-start convergence and current/charge tolerance sensitivity."""
import argparse
import bisect
import gzip
import json
import os
from pathlib import Path
import re
import sys
import tempfile

SOURCE = Path(__file__).resolve()
ROOT = SOURCE.parents[3]
sys.path[:0] = [str(SOURCE.parents[1]), str(ROOT / 'VerilogA'), str(ROOT / '1G/tests'),
               str(SOURCE.parent), str(ROOT / 'pdk/1G_repeater_IHPSG13G2')]
from compact_trace import checked_rows
from extract_analog import digest
from input_charge import verify_files
from reserved_trace import capture_reserved_trace
from tool_process import configure_cleanup, execute, memory_slot
from transmit_connected import run as prepare
from transmit_connected_deck import solver_options


def compare(rows_a, rows_b):
    """Bound every voltage observation; never extrapolate missing startup data."""
    if len(rows_a[0]) != len(rows_b[0]) or rows_a[-1][0] != rows_b[-1][0]:
        raise ValueError('different startup interfaces or stop times')
    times = [row[0] for row in rows_b]
    if rows_a[0][0] < times[0]:
        raise ValueError('refined waveform omits startup')
    maximum = [0.] * (len(rows_a[0]) - 1)
    for row in rows_a:
        index = min(max(1, bisect.bisect_left(times, row[0])), len(rows_b) - 1)
        low, high = rows_b[index - 1], rows_b[index]
        fraction = (row[0] - low[0]) / (high[0] - low[0])
        if not 0 <= fraction <= 1:
            raise ValueError('startup comparison would extrapolate')
        for column in range(1, len(row)):
            voltage = low[column] + fraction * (high[column] - low[column])
            maximum[column - 1] = max(maximum[column - 1], abs(row[column] - voltage))
    # Numerical variation may use at most one tenth of the loaded transmitter's
    # separate 10 mV physical/schematic comparison allowance.
    if max(maximum) > .001:
        raise ValueError('startup tolerance sensitivity exceeds 1 mV')
    return dict(maximum_voltage_difference_V=max(maximum), per_column_difference_V=maximum,
                limit_V=.001, checked_points=len(rows_a))


def run(args):
    args.work.mkdir(parents=True, exist_ok=True)
    work = Path(tempfile.mkdtemp(prefix='run-', dir=args.work.resolve()))
    options = argparse.Namespace(**vars(args))
    options.work, options.emit_only = work / 'prepared', True
    prepare(options)
    emitted = json.loads((options.work / 'checks.json').read_text())
    origin = Path(emitted['work']) / 'schematic'
    source = (origin / 'bench.cir').read_text()
    header = json.loads((origin / 'columns.json').read_text())
    hashes = dict(emitted['source_sha256'])
    hashes[str(SOURCE)] = digest(SOURCE)
    hashes[str(ROOT / 'VerilogA/reserved_trace.py')] = digest(ROOT / 'VerilogA/reserved_trace.py')
    report = dict(status='running', work=str(work), source_sha256=hashes, cases={},
        memory_exclusive=not args.concurrent, simulation_address_space_bytes=805306368,
        scope='complete component schematic, cold 64 ns startup before controller switching; '
              'numerical convergence regression only, full TX operation checked separately')

    def save():
        text = json.dumps(report, indent=2) + '\n'
        (work / 'checks.json').write_text(text)
        (args.work / 'checks.json').write_text(text)

    try:
        save()
        traces = []
        env = dict(os.environ, PDK_ROOT=str(args.pdk.resolve()), PDK='ihp-sg13g2',
            SPICE_USERINIT_DIR=str(args.pdk.resolve() / 'ihp-sg13g2/libs.tech/ngspice'), OMP_NUM_THREADS='1')
        for tight in (False, True):
            name = 'tight' if tight else 'nominal'
            directory = work / name
            directory.mkdir()
            text, count = re.subn(r'^tran (\S+) \S+ 0 (\S+) uic$', r'tran \1 64n 0 \2 uic', source, flags=re.M)
            if count != 1 or text.count(solver_options()) != 1:
                raise ValueError('changed full-TX numerical profile or transient')
            text = text.replace(solver_options(), solver_options(tight))
            (directory / 'bench.cir').write_text(text)
            hashes[str(directory / 'bench.cir')] = digest(directory / 'bench.cir')
            case = report['cases'][name] = dict(status='running', work=str(directory))
            report['stage'] = 'waiting_' + name
            save()
            with memory_slot(['transmit-startup'], exclusive=not args.concurrent):
                verify_files(hashes)
                report['stage'] = name
                save()
                with capture_reserved_trace(directory / 'trace.dat', 32 * 1024 ** 2) as captured:
                    with (directory / 'ngspice.log').open('w') as log:
                        execute(['prlimit', '--as=805306368', '--', args.ngspice.resolve(), '-b', 'bench.cir'],
                            cwd=directory, env=env, stdout=log, stderr=-2, timeout=1800,
                            check=True, memory_exclusive=False)
                log = (directory / 'ngspice.log').read_text()
                if ('ngspice-47 done' not in log or
                        re.search(r'\b(error|fatal|failed|aborted)\b|timestep too small', log, re.I)):
                    raise ValueError(name + ' startup did not converge')
                with gzip.open(captured.path, 'rt') as stream:
                    columns = next(stream).split()
                    if len(columns) != len(header) or columns[0] != 'time':
                        raise ValueError('missing startup observations')
                    rows = checked_rows((line.split() for line in stream if line.strip()), len(header), str(captured.path))
                if (not 0 <= rows[0][0] <= 1e-12 or abs(rows[-1][0] - 64e-9) > 1e-15 or
                        max(b[0] - a[0] for a, b in zip(rows, rows[1:])) > 7.813e-12 or
                        re.findall(r'No[.] of Data Rows\s*:\s*(\d+)', log) != [str(len(rows))]):
                    raise ValueError('incomplete startup trajectory')
                traces.append(rows)
                case.update(status='pass', rows=len(rows), trace_storage=captured.metadata)
            save()
        report['comparison'] = compare(*traces)
        verify_files(hashes)
        report.update(status='startup_numerics_pass', stage='complete')
        print('PASS full-TX 64 ns cold startup and 1 mV numerical tolerance sensitivity')
    except BaseException as error:
        report.update(status='fail', error=str(error))
        raise
    finally:
        save()


if __name__ == '__main__':
    configure_cleanup()
    parser = argparse.ArgumentParser(description=__doc__)
    for name in ('exporter', 'fixture', 'proofs', 'pdk', 'ngspice', 'work'):
        parser.add_argument('--' + name, type=Path, required=True)
    parser.add_argument('--corner', default='mos_tt', choices=('mos_tt',))
    parser.add_argument('--temperature', type=float, default=27)
    parser.add_argument('--trim-code', type=int, choices=range(64), default=44)
    parser.add_argument('--cxx', default='/usr/bin/c++')
    parser.add_argument('--concurrent', action='store_true',
        help='Use a separately budgeted 768 MiB slot instead of shared EDA admission')
    run(parser.parse_args())
