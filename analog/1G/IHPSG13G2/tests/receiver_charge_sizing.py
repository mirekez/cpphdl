#!/usr/bin/env python3
"""Size sampler cancellation using a checked initialized full-bank diagnostic.

The baseline must reproduce each retained continuous-startup acquisition within
1 mV before any sizing result is usable. This is a diagnostic, not a replacement
for the full startup, closed conversion and process-corner regression tests.
"""
import argparse
import bisect
import json
import os
from pathlib import Path
import re
import sys
import tempfile

SOURCE = Path(__file__).resolve()
ANALOG = SOURCE.parents[3]
sys.path[:0] = [str(SOURCE.parent), str(ANALOG / '1G/tests'), str(ANALOG / 'VerilogA')]
from programmable_receive_array import HEADER, WARMUP, read_spice, digest
from trace_storage import capture_trace
from hardware import configure_cleanup, execute


def metrics(rows, offset, samples=20):
    times = [row[0] for row in rows]
    errors, signed = [], []
    for sample in range(samples):
        close = offset + (sample * 4 + 2.5) * 1e-9
        t = close + .05e-9
        index = bisect.bisect_left(times, t)
        if not 0 < index < len(rows):
            raise ValueError('diagnostic target outside trajectory')
        a, b = rows[index - 1], rows[index]
        target = a[4] + (b[4] - a[4]) * (t - a[0]) / (b[0] - a[0])
        window = rows[bisect.bisect_left(times, close + .35e-9):bisect.bisect_right(times, close + 1e-9)]
        if len(window) < 10:
            raise ValueError('missing initialized acquisition aperture')
        differences = [row[8 + 2 * (sample % 16)] - target for row in window]
        errors.append(max(abs(v) for v in differences))
        signed.append(max(differences, key=abs))
    return dict(maximum_acquisition_error_V=max(errors), per_sample_error_V=errors,
                per_sample_signed_error_V=signed)


def initialized_deck(original, held):
    def shift(line):
        match = re.fullmatch(r'(Vsource\d+ \S+ 0) PWL\(([^)]*)\)', line)
        if not match:
            return line
        numbers = list(map(float, match[2].split()))
        points = list(zip(numbers[::2], numbers[1::2]))
        current = points[0][1]
        for (a, value), (b, following) in zip(points, points[1:]):
            if b <= WARMUP:
                current = following
            elif a < WARMUP < b:
                current = value + (following - value) * (WARMUP - a) / (b - a)
                break
            elif a >= WARMUP:
                break
        shifted = [(0., current)] + [(t - WARMUP, v) for t, v in points if t > WARMUP]
        return match[1] + ' PWL(' + ' '.join(f'{t:.17g} {v:.17g}' for t, v in shifted) + ')'

    lines, transients = [], 0
    for line in original.splitlines():
        if line.startswith('.ic '):
            match = re.fullmatch(r'\.ic V\((n\d+)\)=(\S+)', line)
            if not match:
                raise ValueError('changed initial-voltage declaration')
            line = (f'.ic V({match[1]})={held[match[1]]:.17g}' if match[1] in held
                    else '.nodeset' + line[3:])
        elif line.startswith('C'):
            line = re.sub(r' IC=\S+$', '', line)
        elif line.startswith('Vsource'):
            line = shift(line)
        elif line.startswith('Vsim_timestep_grid '):
            line = 'Vsim_timestep_grid cpphdl_sim_timestep_grid 0 PULSE(0 0 0 20p 20p 20p 80p)'
        elif line.startswith('tran '):
            line = 'tran 20p 80n 0 20p'
            transients += 1
        lines.append(line)
    if transients != 1:
        raise ValueError('changed sizing transient interface')
    return '\n'.join(lines) + '\n'


def run(args):
    root = args.work.resolve()
    root.mkdir(parents=True, exist_ok=True)
    work = Path(tempfile.mkdtemp(prefix='sweep-', dir=root))
    receipt = json.loads(args.receipt.read_text())
    if receipt.get('sampler_dummy_p_width_um') != 30 or not receipt.get('charge_defined_nodes'):
        raise ValueError('diagnostic requires the actual 30 um charge-defined reference')
    case = Path(receipt['run_directory']) / str(args.code)
    reference = receipt['cases'][str(args.code)]
    for path, value in receipt['source_sha256'].items():
        if digest(path) != value:
            raise ValueError('stale reference experiment: ' + path)
    for path, value in reference['artifact_sha256'].items():
        if digest(path) != value:
            raise ValueError('changed reference artifact: ' + path)
    paths = [SOURCE, args.receipt.resolve(), args.ngspice.resolve(), ANALOG / 'VerilogA/trace_storage.py',
             SOURCE.with_name('programmable_receive_array.py'), case / 'physical.cir', case / 'bindings.json',
             Path(reference['waveform'])]
    hashes = {str(p): digest(p) for p in paths} | receipt['source_sha256']
    rows = read_spice(Path(reference['waveform']), HEADER)
    original = (case / 'physical.cir').read_text()
    observations = dict((int(i), n) for i, n in re.findall(r'^let observation(\d+) = v\((n\d+)\)$', original, re.M))
    held = {observations[7 + 2 * i]: rows[0][8 + 2 * i] for i in range(16)}
    baseline = metrics(rows, WARMUP)
    base = initialized_deck(original, held)
    bindings = json.loads((case / 'bindings.json').read_text())
    dummy = [d['device'] for d in bindings if d['source'] == 'mos_track_hold.hpp'
             and d['model'] == 'sg13_hv_pmos' and d['width_um'] == 30]
    if len(dummy) != 16:
        raise ValueError('expected exactly sixteen cancellation PMOS devices')
    report = dict(status='running', source_sha256=hashes, work=str(work), cases={}, reference=baseline,
                  scope='initialized 80 ns full sixteen-slice sizing diagnostic; baseline reproduction required; no full-startup, closed ADC or PVT qualification')

    def save():
        payload = json.dumps(report, indent=2) + '\n'
        (work / 'checks.json').write_text(payload)
        (root / 'checks.json').write_text(payload)

    try:
        models = args.pdk.resolve() / 'ihp-sg13g2/libs.tech/ngspice'
        environment = dict(os.environ, PDK_ROOT=str(args.pdk.resolve()), PDK='ihp-sg13g2',
                           SPICE_USERINIT_DIR=str(models), OMP_THREAD_LIMIT='1')
        for width, fingers in ((30, 3), (40, 4), (50, 5)):
            current = work / str(width)
            current.mkdir()
            deck = base
            for identifier in dummy:
                pattern = rf'^(Xphysical{identifier} .* sg13_hv_pmos) w=30u l=0.45u ng=3$'
                deck, count = re.subn(pattern, rf'\1 w={width}u l=0.45u ng={fingers}', deck, flags=re.M)
                if count != 1:
                    raise ValueError('changed cancellation device: ' + identifier)
            (current / 'physical.cir').write_text(deck)
            with capture_trace(current / 'spice.dat') as captured, (current / 'ngspice.log').open('w') as log:
                execute([str(args.ngspice.resolve()), '-b', 'physical.cir'], cwd=current, env=environment,
                        stdout=log, stderr=-2, check=True, timeout=2400)
            log = (current / 'ngspice.log').read_text()
            for line in log.replace('\r', '\n').splitlines():
                if line == 'Warning: Dynamic gmin stepping failed' and 'Note: True gmin stepping completed' in log:
                    continue
                if re.search(r'\b(error|fatal|failed|aborted|unknown)\b|timestep too small', line, re.I):
                    raise ValueError('failed sizing simulation: ' + line)
            if 'ngspice-47 done' not in log:
                raise ValueError('incomplete sizing simulation')
            observed = read_spice(captured.path, HEADER)
            if abs(observed[-1][0] - 80e-9) > 1e-14:
                raise ValueError('missing complete diagnostic window')
            result = metrics(observed, 0)
            result.update(waveform=str(captured.path), waveform_capture=captured.metadata,
                          artifact_sha256={str(p): digest(p) for p in current.iterdir() if p.is_file()})
            report['cases'][str(width)] = result
            if width == 30:
                delta = max(abs(a - b) for a, b in zip(result['per_sample_error_V'], baseline['per_sample_error_V']))
                report['baseline_reproduction_maximum_difference_V'] = delta
                if delta > .001:
                    raise ValueError('initialized diagnostic does not reproduce continuous startup within 1 mV')
            save()
            print(width, 'um, maximum diagnostic error V', result['maximum_acquisition_error_V'], flush=True)
        if any(digest(p) != value for p, value in hashes.items()):
            raise ValueError('sizing experiment inputs changed')
        report['status'] = 'diagnostic_complete'
    except BaseException as error:
        report.update(status='fail', error=str(error))
        raise
    finally:
        save()


if __name__ == '__main__':
    configure_cleanup()
    parser = argparse.ArgumentParser(description=__doc__)
    for name in ('work', 'receipt', 'pdk', 'ngspice'):
        parser.add_argument('--' + name, type=Path, required=True)
    parser.add_argument('--code', choices=(4, 8), type=int, default=8)
    run(parser.parse_args())
