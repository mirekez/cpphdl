#!/usr/bin/env python3
"""Qualify complete clock pulses independently in native, VA and IHP PSP flows."""
import argparse
import json
import math
import os
from pathlib import Path
import sys
import tempfile

SOURCE = Path(__file__).resolve()
ANALOG = SOURCE.parents[3]
sys.path[:0] = [str(SOURCE.parent.parent), str(ANALOG / 'VerilogA'), str(ANALOG / '1G/tests')]
from processing_clock_gate_process import process_deck
from compare import read_cpp, read_spice, interpolate
from hardware import configure_cleanup, execute, source_dependencies
from receive_sample_recovery import digest, archive

HEADER = ['time_s', 'CLOCK', 'ENABLE', 'OUT', 'HELD', 'VDD']
STOP = 15e-9 + 32 * 2.5e-9 + 64 / 134e6


def clock_schedule():
    time, pulses = 10e-9, []
    for cycle in range(96):
        period = 2.5e-9 if cycle < 16 or 48 <= cycle < 64 else 1 / 134e6
        pulses.append((time + 25e-12, time + period / 2 + 25e-12))
        time += period
    return pulses


def schedule():
    return [pulse for cycle, pulse in enumerate(clock_schedule())
            if 21 <= cycle <= 36 or 69 <= cycle <= 84]


def crossings(rows, column, level, rising):
    result = []
    for a, b in zip(rows, rows[1:]):
        if b[0] < 8e-9:
            continue
        if (a[column] < level <= b[column]) if rising else (a[column] > level >= b[column]):
            result.append(a[0] + (b[0] - a[0]) * (level - a[column]) / (b[column] - a[column]))
    return result


def qualify(rows, supply):
    if (not rows or any(len(row) != 6 or not all(math.isfinite(v) for v in row) for row in rows)
            or not 0 <= rows[0][0] <= 10e-12 or abs(rows[-1][0] - STOP) > 1e-14
            or any(not 0 < b[0] - a[0] <= 10.1e-12 for a, b in zip(rows, rows[1:]))):
        raise ValueError('incomplete clock admission waveform')
    for direction in (True, False):
        observed = crossings(rows, 1, supply / 2, direction)
        wanted = [pulse[0 if direction else 1] for pulse in clock_schedule()]
        if len(observed) != 96 or any(abs(a - b) > 100e-12 for a, b in zip(observed, wanted)):
            raise ValueError('raw acquisition/settled clock stimulus did not reach the gate')
    rising, falling = [crossings(rows, 3, supply / 2, direction) for direction in (True, False)]
    expected = schedule()
    if len(rising) != 32 or len(falling) != 32:
        raise ValueError('extra, missing or cut admitted clock pulses')
    delays = []
    for rise, fall, (wanted_rise, wanted_fall) in zip(rising, falling, expected):
        delays.extend((rise - wanted_rise, fall - wanted_fall))
        if not 3e-9 <= fall - rise <= 4.1e-9:
            raise ValueError('runt or stretched clock pulse')
    if not all(-20e-12 <= delay <= 400e-12 for delay in delays):
        raise ValueError('clock admitted in the wrong phase or delayed beyond budget')
    times = [r[0] for r in rows]
    levels = [interpolate(rows, times, (a + b) / 2, 3) for a, b in zip(rising, falling)]
    late = [row for row in rows if row[0] >= 8e-9]
    if min(levels) < .9 * supply or min(r[5] for r in late) < .95 * supply:
        raise ValueError('admitted clock or supply does not reach the required rail')
    if any(not -.1 <= r[c] <= supply + .1 for r in late for c in range(1, 6)):
        raise ValueError('clock admission electrical over/undershoot')
    return dict(pulses=32, blocked_acquisition_cycles=32,
                minimum_high_s=min(b - a for a, b in zip(rising, falling)),
                maximum_edge_delay_s=max(delays), minimum_high_V=min(levels))


def run(args):
    args.work.mkdir(parents=True, exist_ok=True)
    work = Path(tempfile.mkdtemp(prefix='run-', dir=args.work.resolve()))
    hashes = source_dependencies([SOURCE.with_suffix('.cpp')], ANALOG.parent, args.cxx,
        ['-std=c++20', '-I' + str(ANALOG), '-I' + str(ANALOG / 'include'), '-I/usr/include/eigen3'])
    inputs = [SOURCE, SOURCE.parent.parent / 'processing_clock_gate_process.py',
              SOURCE.parent.parent / 'core_driver_process.py', SOURCE.parent.parent / 'mos_geometry.py',
              ANALOG / 'VerilogA/compare.py', ANALOG / '1G/tests/hardware.py',
              ANALOG / '1G/tests/receive_sample_recovery.py',
              ANALOG / 'pdk/1G_repeater_IHPSG13G2/tool_process.py',
              args.bench.resolve(), args.ngspice.resolve(), args.openvaf.resolve()]
    models = args.pdk.resolve() / 'ihp-sg13g2/libs.tech/ngspice/models'
    inputs += list(models.rglob('*.lib')) + list(models.rglob('*.spice'))
    hashes.update({str(p): digest(p) for p in inputs})
    report = dict(status='running', work=str(work), source_sha256=hashes, flows={},
                  scope='electrical pulse admission at 20 fF load; excludes clock tree, layout and controller integration')

    def save():
        text = json.dumps(report, indent=2) + '\n'
        (work / 'checks.json').write_text(text)
        (args.work / 'checks.json').write_text(text)

    def command(directory, name, argv, environment=None):
        with (directory / (name + '.log')).open('w') as log:
            execute(list(map(str, argv)), cwd=directory, stdout=log, stderr=-2,
                    check=True, timeout=600, memory_exclusive=False, env=environment)

    save()
    try:
        waves = {}
        cases = [('cpp', None, 27, 1.2), ('veriloga', None, 27, 1.2),
                 ('psp_tt', 'mos_tt', 27, 1.2), ('psp_ss', 'mos_ss', 125, 1.08),
                 ('psp_ff', 'mos_ff', -40, 1.32)]
        for flow, corner, temperature, supply in cases:
            destination = work / flow
            destination.mkdir()
            command(destination, 'bench', [args.bench.resolve(), destination, 'run' if flow == 'cpp' else 'export'])
            if flow == 'cpp':
                trace = destination / 'cpp/trace.csv'
                header, rows = read_cpp(trace)
                if header != HEADER:
                    raise ValueError('changed native clock admission interface')
            else:
                environment = dict(os.environ, OMP_NUM_THREADS='1')
                if corner:
                    deck, binding = process_deck((destination / 'design.va').read_text(),
                        (destination / 'bench.cir').read_text(), models, corner, temperature, supply)
                    (destination / 'process.json').write_text(json.dumps(binding, indent=2) + '\n')
                    (destination / 'bench.cir').write_text(deck)
                    environment.update(PDK_ROOT=str(args.pdk.resolve()), PDK='ihp-sg13g2',
                        SPICE_USERINIT_DIR=str(args.pdk.resolve() / 'ihp-sg13g2/libs.tech/ngspice'))
                else:
                    command(destination, 'openvaf', [args.openvaf.resolve(), 'design.va', '-o', 'design.osdi'])
                command(destination, 'ngspice', [args.ngspice.resolve(), '-b', 'bench.cir'], environment)
                trace = destination / 'spice.dat'
                rows = read_spice(trace, HEADER)
            report['flows'][flow] = qualify(rows, supply)
            report['flows'][flow]['trace'] = archive(trace)
            if flow in ('cpp', 'veriloga'):
                waves[flow] = rows
            save()
        cpp, va = waves['cpp'], waves['veriloga']
        # SPICE can begin at its first positive integration step. The complete
        # coverage checks above bound that difference; never extrapolate a rail.
        shared = [r for r in cpp if va[0][0] <= r[0] <= va[-1][0]]
        if len(cpp) - len(shared) > 2:
            raise ValueError('too much trajectory excluded from common-time comparison')
        times = [r[0] for r in va]
        errors = [sum((r[column] - interpolate(va, times, r[0], column)) ** 2 for r in shared) / len(shared)
                  for column in (3, 4)]
        rms = [math.sqrt(value) for value in errors]
        if max(rms) > .02:
            raise ValueError('native/Verilog-A output or stored-admission RMS exceeds 20 mV')
        report['comparison'] = dict(output_rms_V=rms[0], held_rms_V=rms[1], limit_V=.02,
                                    compared_samples=len(shared), boundary_samples=len(cpp) - len(shared))
        if any(digest(Path(p)) != h for p, h in hashes.items()):
            raise ValueError('clock admission qualification sources changed')
        report['artifact_sha256'] = {str(p): digest(p) for p in work.rglob('*')
                                    if p.is_file() and p.name in ('design.va', 'bench.cir', 'process.json', 'pulses.csv')}
        report['status'] = 'pass'
        print('PASS physical clock admission: native, Verilog-A, three PSP corners and comparison')
    except BaseException as error:
        report.update(status='fail', error=str(error))
        raise
    finally:
        save()


if __name__ == '__main__':
    configure_cleanup()
    parser = argparse.ArgumentParser(description=__doc__)
    for name in ('bench', 'work', 'pdk', 'ngspice', 'openvaf'):
        parser.add_argument('--' + name, type=Path, required=True)
    parser.add_argument('--cxx', default='/usr/bin/c++')
    run(parser.parse_args())
