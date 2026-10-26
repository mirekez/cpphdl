#!/usr/bin/env python3
"""Compare independently qualified electrical clock/controller acquisitions."""
import argparse
import csv
import gzip
import hashlib
import json
import math
from pathlib import Path
import resource

from processing_clock_delivery import HEADER, qualify, digest, edges, interpolate
from compact_trace import checked_rows
from tool_process import memory_slot

SOURCE = Path(__file__).resolve()
LIMITS = dict(frequency_relative=.01, period_normalized_rms_V=.06,
              # One acquisition iteration: withdrawal, three measurement
              # windows, then the registered measurement's decision edge.
              code_adjusted_control_V=.002, supply_V=.005, readiness_reference_cycles=32 + 3 * 128 + 1)


def trim_fraction(trim):
    # Each ideal 10 kohm R/2R ladder has 10 kohm output resistance. Including
    # the 10 kohm coarse and 5.09 Mohm fine mixing resistors gives conductance
    # weights 255:1. MOS rail-driver errors remain in the measured residual.
    return (255 * (trim >> 8) + (trim & 255)) / 65536


def control_residual(control, supply, trim):
    return control - supply * trim_fraction(trim)


def read_trace(path, flow, expected_hash):
    checksum = hashlib.sha256()
    with gzip.open(path, 'rb') as stream:
        for block in iter(lambda: stream.read(1024 * 1024), b''):
            checksum.update(block)
    if checksum.hexdigest() != expected_hash:
        raise ValueError('retained closed-clock waveform changed')
    with gzip.open(path, 'rt') as stream:
        if flow == 'cpp':
            reader = csv.reader(stream)
            if next(reader) != HEADER:
                raise ValueError('changed native closed-clock observations')
        else:
            names = next(stream).split()
            if len(names) != len(HEADER) or names[0] != 'time':
                raise ValueError('changed SPICE closed-clock observations')
            reader = (line.split() for line in stream if line.strip())
        return checked_rows(reader, len(HEADER), str(path))


def measured_flow(report, flow, input_hashes):
    root = Path(report['work'])
    trace = root / ('trace.csv.gz' if flow == 'cpp' else 'spice.dat.gz')
    controls = root / ('controls.csv' if flow == 'cpp' else 'events.csv')
    for path in (trace, controls):
        input_hashes[str(path.resolve())] = digest(path)
    rows = read_trace(trace, flow, report['trace_sha256'])
    with controls.open() as stream:
        records = list(csv.DictReader(stream))
    # Recompute the complete electrical and controller contract. A saved pass
    # flag alone cannot qualify a truncated waveform or changed event log.
    metrics = qualify(rows, records, flow)
    if metrics != report['electrical']:
        raise ValueError('closed-clock measurements differ from the retained receipt')
    references = records[1:] if flow == 'cpp' else [r for r in records if int(r['ref_edge'])]
    last_bad = max((i for i, r in enumerate(references)
                    if not (int(r['ready_ref']) and int(r['ready_pcs']))), default=-1)
    crossings = edges(rows, 1, .6, 60e-6, 64e-6)
    times = [r[0] for r in rows]
    shapes = [[sum(interpolate(rows, times, a + (b - a) * i / 128, column)
                   for a, b in zip(crossings[-9:-1], crossings[-8:])) / 8
               for i in range(128)] for column in (1, 2, 3, 4, 6, 8)]
    # Phase and trim need not match: each controller closes its own loop and
    # the count boundary can select a neighboring code. Compare achieved
    # frequency, period shape, tuning voltage and time to sustained readiness.
    return dict(electrical=metrics, shapes=shapes,
                ready_reference_index=last_bad + 1,
                control_mean_V=sum(shapes[2]) / len(shapes[2]),
                supply_mean_V=sum(shapes[3]) / len(shapes[3]))


def evaluate(cpp, veriloga):
    input_hashes = {str(p.resolve()): digest(p) for p in (SOURCE, cpp, veriloga)}
    reports = [json.loads(p.read_text()) for p in (cpp, veriloga)]
    sources = []
    for report, flow in zip(reports, ('cpp', 'veriloga')):
        if report.get('status') != 'pass' or report.get('flow') != flow:
            raise ValueError('missing independently passed closed-clock flow: ' + flow)
        hashes = report.get('source_sha256', {})
        if not hashes or any(digest(Path(p)) != h for p, h in hashes.items()):
            raise ValueError('stale closed-clock source/tool evidence')
        sources.append({str(Path(p).resolve()): h for p, h in hashes.items()})
    required = [SOURCE.with_name('processing_clock_delivery.cpp'),
                SOURCE.with_name('processing_clock_delivery.py'),
                SOURCE.parent.parent / 'processing_clock_delivery.hpp',
                SOURCE.parents[2] / 'processing_clock_control.hpp']
    if any(str(p.resolve()) not in sources[0] or str(p.resolve()) not in sources[1] for p in required):
        raise ValueError('closed-clock circuit/controller provenance is incomplete')
    if any(sources[0][p] != sources[1][p] for p in sources[0].keys() & sources[1].keys()):
        raise ValueError('closed-clock flows used different common sources or tools')
    a = measured_flow(reports[0], 'cpp', input_hashes)
    b = measured_flow(reports[1], 'veriloga', input_hashes)
    frequency = abs(a['electrical']['frequency_Hz'] / b['electrical']['frequency_Hz'] - 1)
    waveform = [math.sqrt(sum((x - y) ** 2 for x, y in zip(left, right)) / 128)
                for left, right in ((a['shapes'][i], b['shapes'][i]) for i in (0, 1, 4, 5))]
    control = abs(a['control_mean_V'] - b['control_mean_V'])
    control_difference = abs(control_residual(a['control_mean_V'], a['supply_mean_V'], a['electrical']['final_trim']) -
                             control_residual(b['control_mean_V'], b['supply_mean_V'], b['electrical']['final_trim']))
    supply = abs(a['electrical']['supply_min_V'] - b['electrical']['supply_min_V'])
    readiness = abs(a['ready_reference_index'] - b['ready_reference_index'])
    passed = (frequency <= LIMITS['frequency_relative'] and max(waveform) <= LIMITS['period_normalized_rms_V']
              and control_difference <= LIMITS['code_adjusted_control_V'] and supply <= LIMITS['supply_V']
              and readiness <= LIMITS['readiness_reference_cycles'])
    for hashes in sources + [input_hashes]:
        if any(digest(Path(p)) != h for p, h in hashes.items()):
            raise ValueError('closed-clock comparison inputs changed during checking')
    return dict(status='pass' if passed else 'fail', limits=LIMITS,
                frequency_relative_difference=frequency,
                period_normalized_rms_V=dict(zip(('RAW', 'RING', 'DELIVERED', 'HELD'), waveform)),
                control_difference_V=control, supply_difference_V=supply,
                code_adjusted_control_difference_V=control_difference,
                sustained_readiness_difference_reference_cycles=readiness,
                electrical=dict(cpp=a['electrical'], veriloga=b['electrical']),
                source_sha256=dict(sources[0], **sources[1]), input_sha256=input_hashes,
                scope='same-equation closed electrical acquisition and admission at 200 fF; excludes PSP equivalence and extracted clock-tree qualification')


def compare(cpp, veriloga, output):
    output.parent.mkdir(parents=True, exist_ok=True)
    evidence = dict(status='running')
    output.write_text(json.dumps(evidence, indent=2) + '\n')
    try:
        evidence = evaluate(cpp, veriloga)
        if evidence['status'] != 'pass':
            raise ValueError('closed-clock solvers disagree; see ' + str(output))
    except BaseException as error:
        evidence.update(status='fail', error=str(error))
        raise
    finally:
        output.write_text(json.dumps(evidence, indent=2) + '\n')
    print('PASS closed processing-clock solvers: acquisition, delivered frequency and period shapes')


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    for name in ('cpp', 'veriloga', 'output'):
        parser.add_argument('--' + name, type=Path, required=True)
    args = parser.parse_args()
    resource.setrlimit(resource.RLIMIT_AS, (2 << 30, 2 << 30))
    with memory_slot(["processing-clock-comparison"], exclusive=True):
        compare(args.cpp, args.veriloga, args.output)
