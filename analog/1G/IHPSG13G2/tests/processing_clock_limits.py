#!/usr/bin/env python3
"""Check the raw oscillator acquisition budget at declared PSP operating points."""
import argparse
import gzip
import json
import math
import os
from pathlib import Path
import re
import shutil
import sys
import tempfile

SOURCE = Path(__file__).resolve()
ANALOG = SOURCE.parents[3]
sys.path[:0] = [str(SOURCE.parent.parent), str(ANALOG / '1G/tests'), str(ANALOG / 'VerilogA')]
from processing_clock_process import process_deck
from processing_clock_oscillator import HEADER
from compare import read_spice, edges, interpolate
from hardware import configure_cleanup, execute, source_dependencies
from reference_clock_domain import digest

# Maximum trim exposes the fast startup corner that the settled-rate tests miss.
# Midscale is the controller's cold-start command. This is a sampled envelope,
# not a claim that every trim, passive tolerance or extracted circuit is covered.
POINTS = [('ff_cold', 'mos_ff', -40, 1.32), ('tt', 'mos_tt', 27, 1.2),
          ('ss_hot', 'mos_ss', 125, 1.08)]
MAXIMUM_HZ = 800e6


def supply_deck(original, supply):
    if not math.isfinite(supply) or not 1.08 <= supply <= 1.32:
        raise ValueError('unsupported acquisition supply')
    dc_count = 0
    clear_count = 0
    result = []
    for line in original.splitlines():
        if line.startswith('Vsource'):
            dc = re.fullmatch(r'(Vsource\d+ n\d+ 0 DC )([\d.eE+-]+)', line)
            clear = re.fullmatch(r'(Vsource\d+ n\d+ 0 PWL\()(.*)\)', line)
            if dc:
                value = float(dc[2])
                if value not in (0, 1.2):
                    raise ValueError('unexpected oscillator rail source')
                line = dc[1] + format(supply if value else 0, '.17g')
                dc_count += 1
            elif clear:
                values = list(map(float, clear[2].split()))
                if (len(values) != 6 or values[1::2] != [1.2, 1.2, 0] or
                        any(abs(a - b) > 1e-20 for a, b in zip(values[::2], [0, 32e-9, 32.1e-9]))):
                    raise ValueError('changed oscillator clear waveform')
                values[1] = values[3] = supply
                line = clear[1] + ' '.join(format(v, '.17g') for v in values) + ')'
                clear_count += 1
            else:
                raise ValueError('unreviewed oscillator source')
        result.append(line)
    if dc_count != 17 or clear_count != 1:
        raise ValueError('missing oscillator rail/clear sources')
    return '\n'.join(result) + '\n'


def measure(rows, supply, maximum_hz=MAXIMUM_HZ, require_start=True):
    if (not math.isfinite(maximum_hz) or maximum_hz <= 0 or len(rows) < 10 or
            rows[0][0] > 5e-12 or abs(rows[-1][0] - 512e-9) > 1e-14):
        raise ValueError('incomplete acquisition trajectory or invalid budget')
    if any(not all(math.isfinite(v) for v in r) or len(r) != len(HEADER) for r in rows):
        raise ValueError('invalid acquisition observations')
    gaps = [b[0] - a[0] for a, b in zip(rows, rows[1:])]
    if min(gaps) <= 0 or max(gaps) > 5.005e-12:
        raise ValueError('unresolved acquisition trajectory')
    # Include the first delivered periods after CLEAR releases, not only a
    # favorable steady-state window. Startup latency is measured separately.
    crossings = edges(rows, 1, supply / 2, 32.1e-9, 512e-9)
    if require_start and (len(crossings) < 20 or crossings[0] > 96e-9 or crossings[-1] < 500e-9):
        raise ValueError('oscillator failed to start and sustain its raw clock')
    active = [r for r in rows if r[0] >= 32.1e-9]
    if min(r[4] for r in active) < .9 * supply:
        raise ValueError('oscillator supply collapse')
    if any(not -.1 <= r[c] <= supply + .1 for r in active for c in (1, 2, 3)):
        raise ValueError('unbounded oscillator electrical trajectory')
    if len(crossings) < 2:
        # The reference-domain search can tune a stopped initial command.
        # No two delivered edges means no violation of the counter's period
        # budget; maximum trim must still start and reach the target band.
        return dict(frequency_Hz=None, maximum_instantaneous_Hz=None,
                    minimum_period_s=None, periods=0, startup_s=None,
                    supply_min_V=min(r[4] for r in active), state='stalled_midscale')
    periods = [b - a for a, b in zip(crossings, crossings[1:])]
    times = [r[0] for r in rows]
    high = [interpolate(rows, times, a + (b - a) / 4, 1) for a, b in zip(crossings, crossings[1:])]
    low = [interpolate(rows, times, a + 3 * (b - a) / 4, 1) for a, b in zip(crossings, crossings[1:])]
    metrics = dict(frequency_Hz=len(periods) / (crossings[-1] - crossings[0]),
                   maximum_instantaneous_Hz=1 / min(periods), minimum_period_s=min(periods),
                   periods=len(periods), startup_s=crossings[0] - 32.1e-9,
                   high_min_V=min(high), low_max_V=max(low),
                   supply_min_V=min(r[4] for r in active))
    if require_start and metrics['frequency_Hz'] < 136e6:
        raise ValueError('maximum trim cannot reach the processing-clock band')
    if metrics['maximum_instantaneous_Hz'] > maximum_hz:
        raise ValueError('raw oscillator exceeds acquisition timing budget: ' + str(metrics))
    if min(high) < .8 * supply or max(low) > .2 * supply:
        raise ValueError('raw oscillator fails clock rail levels')
    if metrics['supply_min_V'] < .9 * supply:
        raise ValueError('oscillator supply collapse')
    if any(not -.1 <= r[c] <= supply + .1 for r in active for c in (1, 2, 3)):
        raise ValueError('unbounded oscillator electrical trajectory')
    return metrics


def run(args):
    args.work.mkdir(parents=True, exist_ok=True)
    work = Path(tempfile.mkdtemp(prefix='run-', dir=args.work.resolve()))
    pdk = args.pdk.resolve()
    models = pdk / 'ihp-sg13g2/libs.tech/ngspice/models'
    hashes = source_dependencies([SOURCE.with_name('processing_clock_oscillator.cpp')], ANALOG.parent,
        args.cxx, ['-std=c++20', '-I' + str(ANALOG), '-I' + str(ANALOG / 'include'), '-I/usr/include/eigen3'])
    for path in [SOURCE, SOURCE.with_name('processing_clock_oscillator.py'),
                 SOURCE.parent.parent / 'processing_clock_process.py',
                 SOURCE.parent.parent / 'core_driver_process.py', SOURCE.parent.parent / 'mos_geometry.py',
                 ANALOG / 'VerilogA/compare.py', ANALOG / '1G/tests/hardware.py',
                 ANALOG / '1G/tests/reference_clock_domain.py',
                 ANALOG / 'pdk/1G_repeater_IHPSG13G2/tool_process.py', args.bench.resolve(),
                 args.ngspice.resolve(), *models.rglob('*.lib'), *models.rglob('*.spice')]:
        hashes[str(path)] = digest(path)
    reuse = None
    if args.reuse_receipt:
        old_path = args.reuse_receipt.resolve()
        reuse = json.loads(old_path.read_text())
        old_hashes = reuse.get('source_sha256', {})
        old_checker = Path(reuse['work']) / 'checker_source.py'
        if (not old_hashes or not old_checker.is_file() or
                digest(old_checker) != old_hashes.get(str(SOURCE)) or
                any(digest(Path(p)) != h for p, h in old_hashes.items() if p != str(SOURCE))):
            raise ValueError('retained simulation inputs changed; waveforms cannot be reused')
        hashes[str(old_path)] = digest(old_path)
        hashes[str(old_checker)] = digest(old_checker)
    report = dict(status='running', work=str(work), source_sha256=hashes, cases={},
                  maximum_Hz=MAXIMUM_HZ,
                  scope='12 PSP schematic operating points with declared passives; excludes full trim/PVT, mismatch and extracted closure')

    def save():
        text = json.dumps(report, indent=2) + '\n'
        (work / 'checks.json').write_text(text)
        (args.work / 'checks.json').write_text(text)

    def command(case, label, argv, environment):
        with (case / (label + '.log')).open('w') as log:
            execute(list(map(str, argv)), cwd=case, env=environment, stdout=log, stderr=-2,
                    check=True, timeout=1800)

    save()
    try:
        env = dict(os.environ, OMP_NUM_THREADS='1', PDK_ROOT=str(pdk), PDK='ihp-sg13g2',
                   SPICE_USERINIT_DIR=str(pdk / 'ihp-sg13g2/libs.tech/ngspice'))
        for name, corner, temperature, supply in POINTS:
            for trim in (65535, 32768):
                for load in (20e-15, 200e-15):
                    case = work / f'{name}_{trim}_{round(load * 1e15)}fF'
                    case.mkdir()
                    command(case, 'export', [args.bench.resolve(), case, 'export', trim, load, 1], env)
                    deck, binding = process_deck((case / 'design.va').read_text(),
                        supply_deck((case / 'bench.cir').read_text(), supply), models, corner, temperature)
                    (case / 'physical.cir').write_text(deck)
                    (case / 'bindings.json').write_text(json.dumps(binding, indent=2) + '\n')
                    trace = case / 'spice.dat'
                    previous = reuse.get('cases', {}).get(case.name) if reuse else None
                    if previous:
                        expected_parameters = dict(trim=trim, load_F=load, corner=corner,
                                                   temperature_C=temperature, supply_V=supply)
                        retained = Path(reuse['work']) / case.name / 'spice.dat.gz'
                        if (previous['parameters'] != expected_parameters or
                                digest(retained) != previous['archive_sha256'] or
                                any(digest(case / n) != previous['artifact_sha256'].get(n)
                                    for n in ('physical.cir', 'bindings.json', 'design.va'))):
                            raise ValueError('reconstructed circuit or retained waveform changed')
                        with gzip.open(retained, 'rb') as src, trace.open('wb') as dst:
                            shutil.copyfileobj(src, dst)
                        if digest(trace) != previous['trace_sha256']:
                            raise ValueError('retained electrical trajectory changed')
                    else:
                        command(case, 'ngspice', [args.ngspice.resolve(), '-b', 'physical.cir'], env)
                    metrics = measure(read_spice(trace, HEADER), supply, require_start=trim == 65535)
                    if previous:
                        metrics['revalidated_waveform'] = str(retained)
                        hashes[str(retained)] = digest(retained)
                    metrics['trace_sha256'] = digest(trace)
                    metrics['parameters'] = dict(trim=trim, load_F=load, corner=corner,
                                                  temperature_C=temperature, supply_V=supply)
                    metrics['artifact_sha256'] = {n: digest(case / n) for n in ('physical.cir', 'bindings.json', 'design.va')}
                    with trace.open('rb') as src, gzip.open(str(trace) + '.gz', 'wb') as dst:
                        shutil.copyfileobj(src, dst)
                    trace.unlink()
                    metrics['archive_sha256'] = digest(Path(str(trace) + '.gz'))
                    report['cases'][case.name] = metrics
                    save()
        if len(report['cases']) != 12 or any(digest(Path(p)) != h for p, h in hashes.items()):
            raise ValueError('missing acquisition cases or changed qualification inputs')
        report['status'] = 'pass'
        print('PASS processing-clock acquisition: 12 PSP operating points below 800 MHz')
    except BaseException as error:
        report.update(status='fail', error=str(error))
        raise
    finally:
        save()


if __name__ == '__main__':
    configure_cleanup()
    parser = argparse.ArgumentParser(description=__doc__)
    for name in ('bench', 'pdk', 'ngspice', 'work'):
        parser.add_argument('--' + name, type=Path, required=True)
    parser.add_argument('--reuse-receipt', type=Path, help='recheck retained waveforms only if reconstructed decks and all simulation inputs match')
    parser.add_argument('--cxx', default='/usr/bin/c++')
    run(parser.parse_args())
