#!/usr/bin/env python3
"""Derive a conservative digital output load from verified PSP translator traces."""
import argparse
import gzip
import hashlib
import json
import math
from pathlib import Path


def digest(path):
    return hashlib.sha256(Path(path).read_bytes()).hexdigest()


def verify_files(expected):
    if not expected:
        raise ValueError('missing source or artifact provenance')
    for name, value in expected.items():
        if digest(name) != value:
            raise ValueError('changed input-charge evidence: ' + name)


def edge_charge(rows, amplitude, resistance=50, startup=32e-9, maximum_step=8e-12):
    """Integrate current over complete edge responses, including ringing.

    Window boundaries are midpoints between command threshold crossings.
    The first and last crossings are excluded so every measured response has
    a full interval on each side. Absolute charge counts returned charge too.
    """
    if amplitude <= 0 or resistance <= 0:
        raise ValueError('positive input swing and source resistance required')
    samples, crossings = [], []
    previous = None
    for time, command, achieved in rows:
        if not all(math.isfinite(x) for x in (time, command, achieved)):
            raise ValueError('nonfinite input-charge waveform')
        if previous is not None:
            if time <= previous[0] or time - previous[0] > maximum_step:
                raise ValueError('incomplete or unordered input-charge waveform')
            a, b = previous[1] - amplitude / 2, command - amplitude / 2
            if (a < 0 <= b) or (a > 0 >= b):
                crossing = previous[0] + (time - previous[0]) * (-a) / (b - a)
                if crossing >= startup:
                    crossings.append(crossing)
        if time >= startup - maximum_step:
            samples.append((time, (command - achieved) / resistance))
        previous = (time, command, achieved)
    if len(crossings) < 3:
        raise ValueError('insufficient complete input transitions')
    windows = [((crossings[i - 1] + crossings[i]) / 2,
                (crossings[i] + crossings[i + 1]) / 2)
               for i in range(1, len(crossings) - 1)]
    measurements = []
    cursor = 0
    for begin, end in windows:
        if samples[0][0] > begin or samples[-1][0] < end:
            raise ValueError('truncated input-charge response')
        charge, absolute = 0.0, 0.0
        while cursor + 1 < len(samples) and samples[cursor + 1][0] <= begin:
            cursor += 1
        index = cursor
        while index + 1 < len(samples) and samples[index][0] < end:
            t0, i0 = samples[index]
            t1, i1 = samples[index + 1]
            left, right = max(begin, t0), min(end, t1)
            a = i0 + (i1 - i0) * (left - t0) / (t1 - t0)
            b = i0 + (i1 - i0) * (right - t0) / (t1 - t0)
            duration = right - left
            charge += (a + b) * duration / 2
            if a * b < 0:
                absolute += duration * (a * a + b * b) / (2 * (abs(a) + abs(b)))
            else:
                absolute += (abs(a) + abs(b)) * duration / 2
            index += 1
        measurements.append(dict(begin_s=begin, end_s=end, net_C=charge, absolute_C=absolute))
    return dict(transitions=len(crossings), measured_edges=len(measurements),
                net_effective_capacitance_F=max(abs(x['net_C']) for x in measurements) / amplitude,
                absolute_effective_capacitance_F=max(x['absolute_C'] for x in measurements) / amplitude,
                edges=measurements)


def characterize(report_path, output, limit=160e-15):
    report_path = report_path.resolve()
    proof = json.loads(report_path.read_text())
    if (proof.get('status') != 'pass' or proof.get('expected_rejection') or proof.get('cases') != 150
            or proof.get('clock_MHz') != 250 or proof.get('startup_s') != 32e-9
            or proof.get('topology') != 'clock cascode with 200 fF output correction'):
        raise ValueError('requires the complete passing 150-case PSP clock translator proof')
    verify_files(proof['source_sha256'])
    cases, identities = [], set()
    for experiment in proof['experiments']:
        verify_files(experiment['artifact_sha256'])
        identity = tuple(experiment[k] for k in ('corner', 'temperature_C', 'logic_supply_V', 'supply_V', 'loaded'))
        if identity in identities or not experiment['checks'] or not all(experiment['checks'].values()):
            raise ValueError('duplicate or failed PSP experiment')
        identities.add(identity)
        paths = [Path(p) for p in experiment['artifact_sha256']]
        deck = next(p for p in paths if p.name == 'bench.cir')
        if '\nRINPUT command a 50\n' not in deck.read_text():
            raise ValueError('changed translator source resistance')
        trace = next(p for p in paths if p.name == 'trace.dat.gz')
        with gzip.open(trace, 'rt') as stream:
            header = next(stream).split()
            if header[:7] != ['time', 'v(y)', 'v(yn)', 'v(a)', 'v(vdd)', 'v(vl)', 'v(command)']:
                raise ValueError('changed PSP input-charge observation interface')
            def rows():
                for line in stream:
                    fields = line.split()
                    if fields:
                        if len(fields) != len(header):
                            raise ValueError('incomplete PSP input-charge row')
                        yield float(fields[0]), float(fields[6]), float(fields[3])
            result = edge_charge(rows(), experiment['logic_supply_V'])
        if result['transitions'] != 15 or result['measured_edges'] != 13:
            raise ValueError('incomplete 250 MHz input-charge experiment')
        cases.append(dict(zip(('corner', 'temperature_C', 'logic_supply_V', 'supply_V', 'loaded'), identity),
                          **result))
    expected = {(c, t, v, h, load) for c in ('mos_tt', 'mos_ff', 'mos_ss', 'mos_sf', 'mos_fs')
                for t in (-40, 27, 125) for v, h in ((1.08, 2.97), (1.2, 3.3), (1.32, 3.63),
                                                   (1.08, 3.63), (1.32, 2.97)) for load in (False, True)}
    if identities != expected:
        raise ValueError('incomplete PSP process/supply/temperature coverage')
    worst = max(x['absolute_effective_capacitance_F'] for x in cases)
    result = dict(status='pass' if worst <= limit else 'fail', cases=cases,
                  scope='charge-equivalent load of the tested translator; excludes interconnect and ADC gate loading',
                  method='integral(abs((command - achieved) / 50 ohm)) / command swing',
                  maximum_effective_capacitance_F=worst, load_budget_F=limit,
                  proof_sha256={str(report_path): digest(report_path)},
                  source_sha256={str(Path(__file__).resolve()): digest(__file__)})
    output.parent.mkdir(parents=True, exist_ok=True)
    output.write_text(json.dumps(result, indent=2) + '\n')
    if result['status'] != 'pass':
        raise ValueError('translator charge exceeds the digital output load budget')
    return result


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--proof', type=Path, required=True)
    parser.add_argument('--output', type=Path, required=True)
    args = parser.parse_args()
    result = characterize(args.proof, args.output)
    print(f"Verified 150 PSP cases; maximum input charge load {result['maximum_effective_capacitance_F'] * 1e15:.3f} fF")
