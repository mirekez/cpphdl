#!/usr/bin/env python3
"""Check half-cycle evaluation separately in C++RTL and actual Verilog-A."""
import argparse
import hashlib
import json
import math
import os
from pathlib import Path
import sys
import time

SOURCE = Path(__file__).resolve()
ANALOG = SOURCE.parents[3]
sys.path[:0] = [str(SOURCE.parents[1]), str(ANALOG / 'VerilogA'), str(ANALOG / '1G/tests')]
from compare import read_cpp, read_spice, interpolate
from hardware import execute, source_dependencies, configure_cleanup
from receive_evaluation_process import process_deck

HEADER = ['time_s', 'CLOCK', 'ENABLE', 'OUT', 'VDD']


def qualify(rows, supply=1.2):
    if rows[0][0] > 10e-12 or abs(rows[-1][0] - 138e-9) > 1e-14:
        raise ValueError('missing evaluation trajectory')
    if any(not 0 < b[0] - a[0] <= 10.1e-12 for a, b in zip(rows, rows[1:])):
        raise ValueError('evaluation trajectory has gaps')
    times = [r[0] for r in rows]
    for cycle in range(32):
        start = 10e-9 + cycle * 4e-9
        expected = 4 <= cycle < 12 or 16 <= cycle < 24
        for delta, wanted in [(1.5e-9, False), (3.5e-9, expected)]:
            out = interpolate(rows, times, start + delta, 3)
            if (out < .7 * supply) if wanted else (out > .3 * supply):
                raise ValueError('wrong evaluation phase/enable at cycle ' + str(cycle))
    rising = [b[0] for a, b in zip(rows, rows[1:]) if b[0] >= 10e-9 and a[3] < .5 * supply <= b[3]]
    falling = [b[0] for a, b in zip(rows, rows[1:]) if b[0] >= 10e-9 and a[3] > .5 * supply >= b[3]]
    cycles = [*range(4, 12), *range(16, 24)]
    if len(rising) != 16 or len(falling) != 16:
        raise ValueError('extra/missing evaluation pulse')
    delay = [rise - (12e-9 + cycle * 4e-9) for rise, cycle in zip(rising, cycles)]
    if any(not 0 <= d <= 1e-9 for d in delay):
        raise ValueError('evaluation edge exceeds half-cycle settling budget')
    if any(not 1e-9 <= fall - rise <= 2.5e-9 for rise, fall in zip(rising, falling)):
        raise ValueError('runt or stretched evaluation')
    return dict(pulses=16, disabled_cycles=16, maximum_rise_delay_s=max(delay), load_F=728e-15)


def run(args):
    work = args.work.resolve()
    work.mkdir(parents=True, exist_ok=True)
    report = dict(status='running', flows={}, seconds={}, process_qualified=False,
                  scope='728 fF evaluation gate load screen; generic channel laws, excludes translator feedback/PEX.')
    def save():
        (work / 'checks.json').write_text(json.dumps(report, indent=2) + '\n')
    def stage(name, command, directory=None, environment=None):
        start = time.monotonic()
        with (work / (name + '.log')).open('w') as log:
            execute(list(map(str, command)), cwd=directory or work, env=environment or dict(os.environ, OMP_NUM_THREADS='1'),
                    stdout=log, stderr=-2, check=True, timeout=120)
        report['seconds'][name] = round(time.monotonic() - start, 4)
    hashes = source_dependencies([SOURCE.with_suffix('.cpp')], ANALOG.parent, args.cxx,
        ['-std=c++20', '-I' + str(ANALOG), '-I' + str(ANALOG / 'include')])
    inputs = [SOURCE, ANALOG / 'VerilogA/compare.py', args.bench, args.ngspice, args.openvaf,
              SOURCE.parents[1] / 'receive_evaluation_process.py', SOURCE.parents[1] / 'core_driver_process.py',
              SOURCE.parents[1] / 'mos_geometry.py']
    if args.pdk:
        models = args.pdk.resolve() / 'ihp-sg13g2/libs.tech/ngspice/models'
        inputs += list(models.rglob('*.lib')) + list(models.rglob('*.spice'))
    for path in inputs:
        hashes[str(path.resolve())] = hashlib.sha256(path.read_bytes()).hexdigest()
    report['source_sha256'] = hashes
    save()
    try:
        stage('native', [args.bench.resolve(), work])
        header, cpp = read_cpp(work / 'cpp/trace.csv')
        if header != HEADER: raise ValueError('changed evaluation observations')
        report['flows']['cpphdl'] = qualify(cpp)
        (work / 'design.osdi').unlink(missing_ok=True)
        stage('openvaf', [args.openvaf.resolve(), 'design.va', '-o', 'design.osdi'])
        (work / 'spice.dat').unlink(missing_ok=True)
        stage('ngspice', [args.ngspice.resolve(), '-b', 'bench.cir'])
        va = read_spice(work / 'spice.dat', HEADER)
        report['flows']['veriloga'] = qualify(va)
        shared = [r for r in cpp if va[0][0] <= r[0] <= va[-1][0]]
        if len(cpp) - len(shared) > 2: raise ValueError('incomplete common trajectory')
        times = [r[0] for r in va]
        rms = math.sqrt(sum((r[3] - interpolate(va, times, r[0], 3)) ** 2 for r in shared) / len(shared))
        if rms > .02: raise ValueError('native/Verilog-A output RMS exceeds 20 mV')
        # A plausible-looking constant or inverted output must fail the same
        # independent checker used by each simulator.
        for mode in ('constant', 'inverted'):
            broken = [list(r) for r in cpp]
            for r in broken: r[3] = 0 if mode == 'constant' else 1.2 - r[3]
            try: qualify(broken)
            except ValueError: pass
            else: raise ValueError('checker accepted ' + mode)
        if args.pdk:
            model, original = (work / 'design.va').read_text(), (work / 'bench.cir').read_text()
            for name, corner, temperature, supply in [('tt', 'mos_tt', 27, 1.2),
                    ('ss', 'mos_ss', 125, 1.08), ('ff', 'mos_ff', -40, 1.32)]:
                directory = work / name
                directory.mkdir(exist_ok=True)
                deck, binding = process_deck(model, original, models, corner, temperature, supply)
                (directory / 'bench.cir').write_text(deck)
                (directory / 'process.json').write_text(json.dumps(binding, indent=2) + '\n')
                (directory / 'spice.dat').unlink(missing_ok=True)
                environment = dict(os.environ, OMP_NUM_THREADS='1', PDK_ROOT=str(args.pdk.resolve()),
                    PDK='ihp-sg13g2', SPICE_USERINIT_DIR=str(args.pdk.resolve() / 'ihp-sg13g2/libs.tech/ngspice'))
                stage('psp_' + name, [args.ngspice.resolve(), '-b', 'bench.cir'], directory, environment)
                report['flows']['psp_' + name] = qualify(read_spice(directory / 'spice.dat', HEADER), supply)
            report['process_load_screen_passed'] = True
        if any(hashlib.sha256(Path(p).read_bytes()).hexdigest() != h for p, h in hashes.items()):
            raise ValueError('evaluation inputs changed during checks')
        report.update(status='pass', comparison=dict(output_rms_V=rms, limit_V=.02, samples=len(shared)))
        print('PASS evaluation gate: native and Verilog-A independent checks, 16 pulses, 728 fF load')
    except BaseException as error:
        report.update(status='fail', error=str(error))
        raise
    finally:
        save()


if __name__ == '__main__':
    configure_cleanup()
    parser = argparse.ArgumentParser(description=__doc__)
    for name in ('bench', 'work', 'openvaf', 'ngspice'):
        parser.add_argument('--' + name, type=Path, required=True)
    parser.add_argument('--cxx', default='/usr/bin/c++')
    parser.add_argument('--pdk', type=Path)
    run(parser.parse_args())
