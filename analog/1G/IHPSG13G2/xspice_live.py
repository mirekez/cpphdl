"""Continuous native C++RTL / PSP co-simulation through ngspice's d_cosim ABI."""
import csv
import json
import math
import os
from pathlib import Path
import re

from hardware import execute
from compare import read_spice
from xspice_bridges import dac_bridge_cards

SOURCE = Path(__file__).resolve()


def simulation_timeout(args):
    limit = getattr(args, 'simulation_timeout', None)
    if limit is None:
        # Sixteen continuously switching PSP slices take substantially longer
        # than the unit circuits. Keep a finite limit below the CTest budget.
        return max(3600, 600 * args.slices)
    if not math.isfinite(limit) or limit <= 0:
        raise ValueError('continuous simulation timeout must be finite and positive')
    return limit


def headers(ngspice):
    directory = ngspice.resolve().parents[1] / 'share/ngspice/scripts/src'
    for name in ('cosim.h', 'cmtypes.h', 'miftypes.h'):
        if not (directory / 'ngspice' / name).is_file():
            raise ValueError('ngspice d_cosim development headers are missing: ' + str(directory))
    return directory


def live_flow(args, work, prepare_process):
    timeout = simulation_timeout(args)
    with (work / 'export.log').open('w') as log:
        export_arguments = getattr(args, 'export_arguments', [args.slices, '.25', args.bursts])
        execute([str(args.bench.resolve()), str(work), 'export', *map(str, export_arguments)],
                stdout=log, stderr=-2, check=True, timeout=60)
    metadata = json.loads((work / 'live.json').read_text())
    conditions = json.loads((work / 'conditions.json').read_text())
    edge = conditions.get('driver_edge_s')
    if (isinstance(edge, bool) or not isinstance(edge, (int, float)) or
            not math.isfinite(edge) or edge <= 0):
        raise ValueError('exported driver edge must be finite and positive')
    experiment, setup = prepare_process(work, metadata)
    deck, control = experiment.split('.control\n', 1)
    if (metadata['feedback_low_V'], metadata['feedback_high_V']) != (.36, .84):
        raise ValueError('unknown XSPICE decision voltage domain')
    expected = [(i, bit) for i in range(args.slices) for bit in range(9)]
    if [(d['slice'], d['bit']) for d in metadata['drivers']] != expected:
        raise ValueError('XSPICE controller output ordering differs')
    library = work / 'controller.so'
    with (work / 'controller-build.log').open('w') as log:
        compact = metadata.get('controller_bank_slices')
        if compact not in (None, 13):
            raise ValueError('unsupported compact controller bank')
        execute([args.cxx, '-std=c++17', '-O1', '-fPIC', '-shared',
                 *(['-DCPPHDL_ADC_COMPACT'] if compact == 13 else []),
                 '-I' + str(SOURCE.parents[3] / 'include'), '-I' + str(headers(args.ngspice)),
                 str(SOURCE.parent / 'tests/sim_adc_controller.cpp'), '-o', str(library)],
                # This fixed digital-only adapter compiled below 200 MB in
                # the smoke test; it can coexist with the routed-module job.
                stdout=log, stderr=-2, check=True, timeout=300, memory_exclusive=False)
    outputs = ['host_bit' + str(i) for i in range(9 * args.slices)]
    analog = []
    for driver in metadata['drivers']:
        identifier = driver['id']
        pattern = rf'^Vbit{identifier} (bit{identifier}) 0 PWL\([^\n]+\)$'
        match = re.search(pattern, deck, re.M)
        if not match:
            raise ValueError('changed host control-source law')
        analog.append(match[1])
        deck, count = re.subn(pattern, '', deck, flags=re.M)
        if count != 1:
            raise ValueError('duplicate host control source')
    decisions = ['host_decision' + str(i) for i in range(args.slices)]
    period = metadata['period_s']
    origin = metadata.get('clock_origin_s', 0)
    if not math.isfinite(origin) or origin < 0 or not math.isfinite(period) or period <= 4e-12:
        raise ValueError('invalid XSPICE clock origin or period')
    deck += f'''Vhost_clock host_clock 0 PULSE(0 1 {origin + period / 2:.17g} 2p 2p {period / 2 - 2e-12:.17g} {period:.17g})
Ahost_clock [host_clock] [host_clock_d] host_input
Ahost_decisions [{' '.join(metadata['decisions'])}] [{' '.join(decisions)}] host_input
Ahost_controller [host_clock_d {' '.join(decisions)}] [{' '.join(outputs)}] null host_controller
{dac_bridge_cards(outputs, analog, 'host_output', 'host_controls')}
.model host_input adc_bridge(in_low=.36 in_high=.84 rise_delay=1p fall_delay=1p)
.model host_output dac_bridge(out_low=0 out_high=1 out_undef=.5 t_rise={edge:.17g} t_fall={edge:.17g})
.model host_controller d_cosim(simulation="./controller.so" sim_args=["{args.slices}" "{args.bursts}" "controls.csv" "bridge_events.csv"] delay=1p)
'''
    # The prepared PSP deck uses installed models through the pinned PDK's
    # .spiceinit. Keep one solver thread and one continuous transient history.
    (work / 'cosim.cir').write_text(deck + '.control\nset num_threads=1\n' + control)
    environment = dict(os.environ, OMP_THREAD_LIMIT='1')
    command = [str(args.ngspice.resolve()), '-n', '-b', 'cosim.cir']
    if getattr(args, 'pdk', None) is not None:
        model_root = args.pdk.resolve() / 'ihp-sg13g2/libs.tech/ngspice'
        environment.update(PDK_ROOT=str(args.pdk.resolve()), PDK='ihp-sg13g2', SPICE_USERINIT_DIR=str(model_root))
        command.remove('-n')
    with (work / 'ngspice.log').open('w') as log:
        execute(command, cwd=work, env=environment,
                stdout=log, stderr=-2, check=True, timeout=timeout)
    if re.search(r'\b(error|fatal|unknown|failed|aborted)\b|not found|timestep too small',
                 (work / 'ngspice.log').read_text(), re.I):
        raise ValueError('continuous XSPICE/PSP simulation failed')


def check_bridge(work, count, read_waveform=read_spice):
    """Check delivered event bits against the separately saved analog voltages."""
    metadata = json.loads((work / 'live.json').read_text())
    with (work / 'columns.csv').open() as stream:
        header = next(csv.reader(stream))
    rows = read_waveform(work / 'spice.dat', header)
    with (work / 'controls.csv').open() as stream:
        controls = [{k: int(v) for k, v in row.items()} for row in csv.DictReader(stream)]
    with (work / 'bridge_events.csv').open() as stream:
        events = list(csv.DictReader(stream))
    clocks, period = metadata['clocks'], metadata['period_s']
    origin = metadata.get('clock_origin_s', 0)
    if count != metadata['slices'] or len(controls) != clocks + 1 or len(events) != clocks:
        raise ValueError('incomplete XSPICE controller coverage')
    if [row['clock'] for row in controls] != list(range(-1, clocks)):
        raise ValueError('XSPICE controller clock sequence differs')
    import bisect
    times = [row[0] for row in rows]
    if any(b < a for a, b in zip(times, times[1:])) or abs(times[-1] - origin - clocks * period) > 1e-14:
        raise ValueError('incomplete/unordered XSPICE analog trajectory')

    def at(time, column):
        index = bisect.bisect_left(times, time)
        if not 0 < index < len(rows):
            raise ValueError('XSPICE input aperture outside its analog trajectory')
        a, b = rows[index - 1], rows[index]
        return a[column] + (b[column] - a[column]) * (time - a[0]) / (b[0] - a[0])

    reads, phase = 0, 0.0
    for clock, event in enumerate(events):
        time = float(event['time_s'])
        offset = time - origin - (clock + .5) * period
        mask = controls[clock]['convert'] & ((1 << count) - 1)
        if (int(event['clock']) != clock or not math.isfinite(time) or not 0 <= offset <= 5e-12
                or int(event['active_mask']) != mask):
            raise ValueError('XSPICE sampling event/phase differs')
        phase = max(phase, offset)
        feedback = 0
        for i in range(count):
            if mask & (1 << i):
                values = [at(time + delta, header.index('CORE_DECISION' + str(i))) for delta in (-25e-12, 0, 25e-12)]
                bit = values[1] > .84
                column = header.index('CORE_DECISION' + str(i))
                values += [row[column] for row in rows[bisect.bisect_left(times, time - 25e-12):
                                                      bisect.bisect_right(times, time + 25e-12)]]
                if any(not math.isfinite(v) or .36 <= v <= .84 or (v > .84) != bit for v in values):
                    raise ValueError('unresolved/unstable XSPICE input around its sampling event')
                feedback |= int(bit) << i
                reads += 1
        if int(event['feedback']) != feedback:
            raise ValueError('XSPICE delivered bits disagree with the analog decisions')
    if not reads:
        raise ValueError('XSPICE test did not consume any electrical decisions')
    return dict(decision_reads=reads, maximum_clock_phase_offset_s=phase,
                decision_stability_window_s=50e-12, backend='continuous XSPICE C++RTL/electrical solver')
