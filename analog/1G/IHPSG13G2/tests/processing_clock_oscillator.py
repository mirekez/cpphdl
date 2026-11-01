#!/usr/bin/env python3
"""Qualify the electrical trim range and output buffer of the processing clock."""
import argparse
import gzip
import json
import os
from pathlib import Path
import shutil
import sys
import tempfile

SOURCE = Path(__file__).resolve()
ANALOG = SOURCE.parents[3]
sys.path[:0] = [str(SOURCE.parent.parent), str(ANALOG / '1G/tests'), str(ANALOG / 'VerilogA')]
from processing_clock_process import process_deck
from compare import read_cpp, read_spice, edges, interpolate
from hardware import configure_cleanup, execute, source_dependencies
from reference_clock_domain import digest

HEADER = ['time_s', 'OUT', 'RING', 'CONTROL', 'VDD', 'CLEAR']
CASES = {str(code): (code, 20e-15, False) for code in (28672, 32768, 38528, 40960)}
CASES.update(loaded=(38528, 200e-15, False), refined=(38528, 20e-15, True))


def measure(rows, refined=False):
    if abs(rows[-1][0] - 512e-9) > 1e-14:
        raise ValueError('incomplete oscillator transient')
    if max(b[0] - a[0] for a, b in zip(rows, rows[1:])) > (5e-12 if refined else 10e-12) * 1.001:
        raise ValueError('unresolved oscillator transient')
    crossings = edges(rows, 1, .6, 256e-9, 512e-9)
    if len(crossings) < 3:
        raise ValueError('trim point did not sustain oscillation')
    times = [r[0] for r in rows]
    late = [r for r in rows if r[0] >= 256e-9]
    high = [interpolate(rows, times, a + (b - a) / 4, 1) for a, b in zip(crossings, crossings[1:])]
    low = [interpolate(rows, times, a + 3 * (b - a) / 4, 1) for a, b in zip(crossings, crossings[1:])]
    periods = [b - a for a, b in zip(crossings, crossings[1:])]
    result = dict(frequency_Hz=(len(crossings) - 1) / (crossings[-1] - crossings[0]),
                  high_min_V=min(high), low_max_V=max(low), supply_min_V=min(r[4] for r in late),
                  control_mean_V=sum(r[3] for r in late) / len(late),
                  period_spread_s=max(periods) - min(periods))
    result['checks'] = dict(rails=min(high) > 1.08 and max(low) < .12,
                           supply=result['supply_min_V'] > 1.1,
                           bounded=all(-.1 < r[c] < 1.3 for r in late for c in (1, 2, 3)),
                           steady=result['period_spread_s'] < 80e-12)
    result['status'] = 'pass' if all(result['checks'].values()) else 'fail'
    return result


def run(args):
    args.work.mkdir(parents=True, exist_ok=True)
    work = Path(tempfile.mkdtemp(prefix='run-', dir=args.work.resolve()))
    hashes = source_dependencies([SOURCE.with_suffix('.cpp')], ANALOG.parent, args.cxx,
        ['-std=c++20', '-I' + str(ANALOG), '-I' + str(ANALOG / 'include'), '-I/usr/include/eigen3'])
    for path in (SOURCE, SOURCE.parent.parent / 'processing_clock_process.py',
                 SOURCE.parent.parent / 'core_driver_process.py', SOURCE.parent.parent / 'mos_geometry.py',
                 ANALOG / 'VerilogA/compare.py', ANALOG / '1G/tests/hardware.py',
                 ANALOG / '1G/tests/reference_clock_domain.py',
                 ANALOG / 'pdk/1G_repeater_IHPSG13G2/tool_process.py', args.bench.resolve()):
        hashes[str(path)] = digest(path)
    report = dict(status='running', flow=args.flow, work=str(work), source_sha256=hashes, cases={},
                  scope='electrical trim range and load isolation; excludes closed-loop acquisition and routed-clock qualification')

    def save():
        payload = json.dumps(report, indent=2) + '\n'
        (work / 'checks.json').write_text(payload)
        (args.work / 'checks.json').write_text(payload)

    def command(case, label, argv, env=None):
        with (case / (label + '.log')).open('w') as log:
            execute(list(map(str, argv)), cwd=case, env=os.environ if env is None else env,
                    stdout=log, stderr=-2, check=True, timeout=1800)

    save()
    try:
        env = dict(os.environ, OMP_NUM_THREADS='1')
        if args.flow != 'cpp':
            hashes[str(args.ngspice.resolve())] = digest(args.ngspice.resolve())
        if args.flow == 'veriloga':
            hashes[str(args.openvaf.resolve())] = digest(args.openvaf.resolve())
        if args.flow == 'psp':
            pdk = args.pdk.resolve()
            models = pdk / 'ihp-sg13g2/libs.tech/ngspice/models'
            hashes.update({str(p): digest(p) for p in list(models.rglob('*.lib')) + list(models.rglob('*.spice'))})
            env.update(PDK_ROOT=str(pdk), PDK='ihp-sg13g2',
                       SPICE_USERINIT_DIR=str(pdk / 'ihp-sg13g2/libs.tech/ngspice'))
        for name, (code, load, refined) in CASES.items():
            case = work / name
            case.mkdir()
            command(case, 'bench', [args.bench.resolve(), case, 'run' if args.flow == 'cpp' else 'export',
                                    code, load, int(refined)])
            if args.flow != 'cpp':
                deck = 'bench.cir'
                if args.flow == 'veriloga':
                    command(case, 'openvaf', [args.openvaf.resolve(), 'design.va', '-o', 'design.osdi'])
                else:
                    physical, binding = process_deck((case / 'design.va').read_text(),
                                                      (case / 'bench.cir').read_text(), models)
                    deck = 'physical.cir'
                    (case / deck).write_text(physical)
                    (case / 'bindings.json').write_text(json.dumps(binding, indent=2) + '\n')
                command(case, 'ngspice', [args.ngspice.resolve(), '-b', deck], env)
            trace = case / ('cpp/trace.csv' if args.flow == 'cpp' else 'spice.dat')
            if args.flow == 'cpp':
                header, rows = read_cpp(trace)
                if header != HEADER:
                    raise ValueError('changed oscillator observation interface')
            else:
                rows = read_spice(trace, HEADER)
            result = measure(rows, refined)
            result['trace_sha256'] = digest(trace)
            result['parameters'] = dict(trim=code, load_F=load, refined=refined)
            with trace.open('rb') as source, gzip.open(str(trace) + '.gz', 'wb') as target:
                shutil.copyfileobj(source, target)
            trace.unlink()
            report['cases'][name] = result
            save()
            if result['status'] != 'pass':
                raise ValueError('electrical oscillator failure: ' + name + ' ' + str(result['checks']))
        rates = [report['cases'][str(code)]['frequency_Hz'] for code in (28672, 32768, 38528, 40960)]
        if not (rates[0] < 132e6 and rates[-1] > 136e6 and all(a < b for a, b in zip(rates, rates[1:]))):
            raise ValueError('monotonic trim range does not bracket the processing clock band')
        centre = report['cases']['38528']['frequency_Hz']
        for case, limit in (('loaded', .02), ('refined', .005)):
            if abs(report['cases'][case]['frequency_Hz'] / centre - 1) > limit:
                raise ValueError('oscillator load/refinement sensitivity exceeded: ' + case)
        if any(digest(Path(p)) != h for p, h in hashes.items()):
            raise ValueError('oscillator qualification inputs changed')
        report.update(status='pass', monotonic_range_Hz=[rates[0], rates[-1]],
                      required_band_Hz=[132e6, 136e6])
        print('PASS processing oscillator: ' + args.flow + ', trim range, rails, load and refinement')
    except BaseException as error:
        report.update(status='fail', error=str(error))
        raise
    finally:
        save()


if __name__ == '__main__':
    configure_cleanup()
    parser = argparse.ArgumentParser(description=__doc__)
    for name in ('bench', 'work', 'pdk', 'ngspice', 'openvaf'):
        parser.add_argument('--' + name, type=Path, required=name in ('bench', 'work'))
    parser.add_argument('--flow', choices=('cpp', 'veriloga', 'psp'), required=True)
    parser.add_argument('--cxx', default='/usr/bin/c++')
    run(parser.parse_args())
