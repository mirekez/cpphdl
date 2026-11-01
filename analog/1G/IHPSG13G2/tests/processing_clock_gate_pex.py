#!/usr/bin/env python3
"""Check physical clock-gate terminal waveforms against the native pulse contract."""
import argparse
import json
import math
from pathlib import Path
import sys

SOURCE = Path(__file__).resolve()
sys.path[:0] = [str(SOURCE.parents[3] / 'VerilogA'), str(SOURCE.parents[1]), str(SOURCE.parent)]
from processing_clock_gate import clock_schedule, schedule, crossings, STOP
from compact_trace import read_spice
from extract_analog import digest

HEADER = ['time_s', 'CLOCK', 'ENABLE', 'OUT', 'VDD']


def qualify(rows, supply):
    if supply not in (1.08, 1.2, 1.32):
        raise ValueError('undeclared physical supply')
    if (not rows or any(len(row) != 5 or not all(math.isfinite(v) for v in row) for row in rows)
            or not 0 <= rows[0][0] <= 10e-12 or abs(rows[-1][0] - STOP) > 1e-14
            or any(not 0 < b[0] - a[0] <= 10.1e-12 for a, b in zip(rows, rows[1:]))):
        raise ValueError('incomplete extracted clock-gate waveform')
    for rising in (True, False):
        raw = crossings(rows, 1, supply / 2, rising)
        wanted = [p[0 if rising else 1] for p in clock_schedule()]
        if len(raw) != 96 or any(abs(a - b) > 100e-12 for a, b in zip(raw, wanted)):
            raise ValueError('raw clock stimulus did not reach the extracted input')
    rising, falling = [crossings(rows, 3, supply / 2, direction) for direction in (True, False)]
    if len(rising) != 32 or len(falling) != 32:
        raise ValueError('extra, missing or cut extracted clock pulses')
    delays = []
    for rise, fall, (wanted_rise, wanted_fall) in zip(rising, falling, schedule()):
        delays.extend((rise - wanted_rise, fall - wanted_fall))
        if not 3e-9 <= fall - rise <= 4.1e-9:
            raise ValueError('runt or stretched extracted clock pulse')
    if not all(-20e-12 <= value <= 400e-12 for value in delays):
        raise ValueError('extracted clock phase/delay exceeds the unchanged 400 ps budget')
    late = [row for row in rows if row[0] >= 8e-9]
    if (min(row[4] for row in late) < .95 * supply
            or any(not -.1 <= row[c] <= supply + .1 for row in late for c in range(1, 5))):
        raise ValueError('extracted clock terminal electrical limit')
    high = [row[3] for row in late if any(a + .5e-9 <= row[0] <= b - .5e-9 for a, b in zip(rising, falling))]
    if not high or min(high) < .9 * supply:
        raise ValueError('extracted high pulse does not reach its required rail')
    return dict(pulses=32, blocked_acquisition_cycles=32, samples=len(rows),
        minimum_high_s=min(b - a for a, b in zip(rising, falling)),
        maximum_edge_delay_s=max(delays), minimum_high_V=min(high))


def recheck(args):
    report = dict(status='running', supply_V=args.supply,
                  source_sha256={str(p.resolve()): digest(p) for p in (
                      SOURCE, SOURCE.with_name('processing_clock_gate.py'),
                      SOURCE.parents[3] / 'VerilogA/compact_trace.py', args.trace)},
                  scope='external clock-gate pulse contract only; extraction provenance and process deck must be qualified separately')
    try:
        report['metrics'] = qualify(read_spice(args.trace, HEADER), args.supply)
        report['status'] = 'pass'
        print('PASS extracted clock terminal pulses: ' + json.dumps(report['metrics']))
    except BaseException as error:
        report.update(status='fail', error=str(error))
        raise
    finally:
        args.output.write_text(json.dumps(report, indent=2) + '\n')


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--trace', type=Path, required=True)
    parser.add_argument('--supply', type=float, required=True)
    parser.add_argument('--output', type=Path, required=True)
    recheck(parser.parse_args())
