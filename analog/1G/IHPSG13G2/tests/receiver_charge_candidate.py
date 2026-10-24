#!/usr/bin/env python3
"""Try one cancellation geometry after proving identical initialized inputs."""
import argparse
import json
import os
from pathlib import Path
import re
import sys
import tempfile

SOURCE = Path(__file__).resolve()
sys.path.insert(0, str(SOURCE.parent))
from receiver_charge_sizing import initialized_deck, metrics, read_spice, HEADER, WARMUP, digest
from trace_storage import capture_trace
from hardware import configure_cleanup, execute


def run(args):
    root = args.work.resolve()
    root.mkdir(parents=True, exist_ok=True)
    work = Path(tempfile.mkdtemp(prefix='candidate-', dir=root))
    baseline = json.loads(args.baseline.read_text())
    if baseline.get('status') != 'diagnostic_complete':
        raise ValueError('a completed independently reproduced baseline is required')
    for path, sha in baseline['source_sha256'].items():
        if digest(path) != sha:
            raise ValueError('stale initialized baseline: ' + path)
    original_path = [Path(p) for p in baseline['source_sha256'] if p.endswith('/physical.cir')]
    if len(original_path) != 1:
        raise ValueError('ambiguous baseline circuit')
    original_path = original_path[0]
    original = original_path.read_text()
    baseline_case = baseline['cases']['30']
    for path, sha in baseline_case['artifact_sha256'].items():
        if digest(path) != sha:
            raise ValueError('changed initialized baseline artifact')
    old = metrics(read_spice(Path(baseline_case['waveform']), HEADER), 0)
    if any(old[key] != baseline_case[key] for key in old):
        raise ValueError('initialized baseline checking result changed')
    if max(abs(a - b) for a, b in zip(old['per_sample_error_V'], baseline['reference']['per_sample_error_V'])) > .001:
        raise ValueError('initialized baseline is not representative of continuous startup')
    continuous_wave = next(Path(p) for p in baseline['source_sha256'] if p.endswith('/spice.dat.gz'))
    continuous = read_spice(continuous_wave, HEADER)
    observations = dict((int(i), n) for i, n in re.findall(r'^let observation(\d+) = v\((n\d+)\)$', original, re.M))
    held = {observations[7 + 2 * i]: continuous[0][8 + 2 * i] for i in range(16)}
    deck = initialized_deck(original, held)
    if deck != (Path(baseline_case['waveform']).parent / 'physical.cir').read_text():
        raise ValueError('regenerated initialized baseline circuit is not byte-identical')
    bindings = json.loads((original_path.parent / 'bindings.json').read_text())
    dummy = [d['device'] for d in bindings if d['source'] == 'mos_track_hold.hpp'
             and d['model'] == 'sg13_hv_pmos' and d['width_um'] == 30]
    if len(dummy) != 16:
        raise ValueError('changed baseline cancellation inventory')
    for identifier in dummy:
        pattern = rf'^(Xphysical{identifier} .* sg13_hv_pmos) w=30u l=0.45u ng=3$'
        deck, count = re.subn(pattern, rf'\1 w=35u l=0.45u ng=4', deck, flags=re.M)
        if count != 1:
            raise ValueError('changed cancellation device')
    (work / 'physical.cir').write_text(deck)
    hashes = baseline['source_sha256'] | {str(SOURCE): digest(SOURCE), str(args.baseline.resolve()): digest(args.baseline)}
    report = dict(status='running', source_sha256=hashes, baseline=str(args.baseline.resolve()), work=str(work),
                  candidate=dict(dummy_p_width_um=35, fingers=4), baseline_reused_without_new_simulation=True,
                  scope='fresh initialized full-bank 80 ns candidate; exact baseline regeneration and waveform recheck; no cold-start/PVT/closed-conversion qualification')
    try:
        models = args.pdk.resolve() / 'ihp-sg13g2/libs.tech/ngspice'
        environment = dict(os.environ, PDK_ROOT=str(args.pdk.resolve()), PDK='ihp-sg13g2',
                           SPICE_USERINIT_DIR=str(models), OMP_THREAD_LIMIT='1')
        with capture_trace(work / 'spice.dat') as captured, (work / 'ngspice.log').open('w') as log:
            execute([str(args.ngspice.resolve()), '-b', 'physical.cir'], cwd=work, env=environment,
                    stdout=log, stderr=-2, check=True, timeout=2400)
        log = (work / 'ngspice.log').read_text()
        if 'ngspice-47 done' not in log or re.search(r'\b(error|fatal|failed|aborted|unknown)\b|timestep too small', log, re.I):
            raise ValueError('candidate simulation failed or incomplete')
        rows = read_spice(captured.path, HEADER)
        if abs(rows[-1][0] - 80e-9) > 1e-14:
            raise ValueError('incomplete candidate trajectory')
        report.update(metrics(rows, 0), waveform=str(captured.path), waveform_archive=captured.metadata)
        if any(digest(p) != sha for p, sha in hashes.items()):
            raise ValueError('candidate inputs changed during verification')
        report['artifact_sha256'] = {str(p): digest(p) for p in work.iterdir() if p.is_file()}
        report['status'] = 'diagnostic_complete'
        print('35 um diagnostic maximum error V', report['maximum_acquisition_error_V'], flush=True)
    except BaseException as error:
        report.update(status='fail', error=str(error))
        raise
    finally:
        payload = json.dumps(report, indent=2) + '\n'
        (work / 'checks.json').write_text(payload)
        (root / 'checks.json').write_text(payload)


if __name__ == '__main__':
    configure_cleanup()
    parser = argparse.ArgumentParser(description=__doc__)
    for name in ('work', 'baseline', 'pdk', 'ngspice'):
        parser.add_argument('--' + name, type=Path, required=True)
    run(parser.parse_args())
