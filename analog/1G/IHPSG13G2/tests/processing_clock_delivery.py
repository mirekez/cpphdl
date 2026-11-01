#!/usr/bin/env python3
"""Run the calibration controller from the oscillator's achieved voltage edges."""
import argparse
from collections import Counter
import csv
import gzip
import json
import math
import os
from pathlib import Path
import re
import resource
import shutil
import sys
import tempfile

SOURCE = Path(__file__).resolve()
ANALOG = SOURCE.parents[3]
sys.path[:0] = [str(SOURCE.parent.parent), str(ANALOG / '1G/tests'), str(ANALOG / 'VerilogA')]
from processing_clock_delivery_process import process_deck
from processing_clock_closed import qualify as qualify_oscillator
from bisect import bisect_right
from compare import edges, interpolate
from compact_trace import read_cpp, read_spice, project, checked_rows
from trace_storage import capture_trace
from hardware import configure_cleanup, execute, source_dependencies
from tool_process import memory_slot
from reference_clock_domain import digest
from xspice_live import headers
from xspice_bridges import dac_bridge_cards

HEADER = ['time_s', 'OUT', 'RING', 'CONTROL', 'VDD', 'CLEAR', 'DELIVERED', 'ADMIT', 'HELD']


def bind_live(model, original, models, metadata):
    """Retain the finite-impedance bench drivers outside the PSP IC."""
    pattern = re.compile(r'I\((n\d+), (n\d+)\) <\+ (\(1.0 - V\(cpphdl_bit(\d+)\)\)|V\(cpphdl_bit(\d+)\)) '
                         r'\* V\((n\d+), (n\d+)\) / 50.0; // sim_driver')
    kept, drivers, counts = [], [], Counter()
    for line in model.splitlines():
        match = pattern.fullmatch(line.strip())
        if match:
            a, b, expression, inverted, direct, va, vb = match.groups()
            identifier = int(inverted or direct)
            if (a, b) != (va, vb) or identifier not in metadata['drivers']:
                raise ValueError('changed electrical host driver')
            counts[(identifier, inverted is not None)] += 1
            a, b = ('0' if value == 'n0' else value for value in (a, b))
            factor = f'(1-v(bit{identifier}))' if inverted is not None else f'v(bit{identifier})'
            drivers.append(f'Bhost{len(drivers)} {a} {b} I={factor}*v({a},{b})/50')
        else:
            kept.append(line)
    if counts != Counter({(bit, sign): 1 for bit in metadata['drivers'] for sign in (False, True)}):
        raise ValueError('missing or duplicate physical host-driver branches')
    deck, binding = process_deck('\n'.join(kept) + '\n', original, models)
    return deck.replace('.control\n', '\n'.join(drivers) + '\n.control\n', 1), binding


def qualify(rows, records, flow):
    if not rows or any(len(row) != len(HEADER) or not all(math.isfinite(v) for v in row) for row in rows):
        raise ValueError('invalid clock delivery observations')
    result = qualify_oscillator(project(rows, 6), records, flow)
    def crossings(column, rising):
        out = []
        for a, b in zip(rows, rows[1:]):
            if b[0] < 8e-9:
                continue
            hit = a[column] < .6 <= b[column] if rising else a[column] > .6 >= b[column]
            if hit:
                out.append(a[0] + (b[0] - a[0]) * (.6 - a[column]) / (b[column] - a[column]))
        return out
    raw_rise, raw_fall = crossings(1, True), crossings(1, False)
    rise, fall = crossings(6, True), crossings(6, False)
    admissions = crossings(7, True)
    if not admissions or not rise or rise[0] < admissions[0]:
        raise ValueError('receiver clock escaped before physical admission')
    times = [row[0] for row in rows]
    accepted, delays, widths = set(), [], []
    for edge in rise:
        index = bisect_right(raw_rise, edge) - 1
        if index < 0 or index in accepted or edge - raw_rise[index] > 600e-12:
            raise ValueError('extra or mistimed delivered clock edge')
        accepted.add(index)
        end = bisect_right(fall, edge)
        raw_end = bisect_right(raw_fall, raw_rise[index])
        if end == len(fall) or raw_end == len(raw_fall):
            if edge < rows[-1][0] - 4e-9:
                raise ValueError('delivered clock remained high')
            continue
        fall_delay = fall[end] - raw_fall[raw_end]
        if not -50e-12 <= fall_delay <= 600e-12:
            raise ValueError('clock withdrawal cut or stretched a pulse')
        if not 3e-9 <= fall[end] - edge <= 4.5e-9:
            raise ValueError('receiver received a runt or acquisition-rate pulse')
        if interpolate(rows, times, (edge + fall[end]) / 2, 6) < 1.08:
            raise ValueError('loaded receiver clock does not reach its high rail')
        delays.extend([edge - raw_rise[index], fall_delay])
        widths.append(fall[end] - edge)
    if len(fall) not in (len(rise), len(rise) - 1) or (fall and fall[0] < rise[0]):
        raise ValueError('unpaired receiver clock transition')
    wanted = {i for i, time in enumerate(raw_rise) if 60e-6 <= time <= 64e-6 - 600e-12}
    if len(wanted) < 500 or not wanted <= accepted:
        raise ValueError('qualified receiver clock lost a raw oscillator pulse')
    if any(row[6] > .12 for row in rows if 8e-9 <= row[0] < admissions[0] - 100e-12):
        raise ValueError('receiver clock toggled during acquisition')
    if not widths or any(not -.1 <= row[c] <= 1.3 for row in rows if row[0] >= 8e-9 for c in (6, 7, 8)):
        raise ValueError('invalid admitted clock voltage')
    result.update(delivered_pulses=len(rise), minimum_high_s=min(widths),
                  maximum_edge_delay_s=max(delays), load_F=200e-15)
    return result


def run(args):
    args.work.mkdir(parents=True, exist_ok=True)
    work = Path(tempfile.mkdtemp(prefix='run-', dir=args.work.resolve()))
    hashes = source_dependencies([SOURCE.with_suffix('.cpp')], ANALOG.parent, args.cxx,
        ['-std=c++20', '-I' + str(ANALOG), '-I' + str(ANALOG / 'include'), '-I/usr/include/eigen3'])
    for path in (SOURCE, SOURCE.with_name('processing_clock_closed.py'), SOURCE.parent.parent / 'processing_clock_process.py', SOURCE.with_name('sim_processing_clock_delivery.cpp'), args.bench.resolve(),
                 SOURCE.parent.parent / 'processing_clock_delivery_process.py', SOURCE.parent.parent / 'core_driver_process.py',
                 SOURCE.parent.parent / 'mos_geometry.py', SOURCE.parent.parent / 'xspice_live.py',
                 SOURCE.parent.parent / 'xspice_bridges.py', ANALOG / 'VerilogA/compare.py',
                 ANALOG / 'VerilogA/compact_trace.py', ANALOG / 'VerilogA/trace_storage.py',
                 ANALOG / '1G/tests/reference_clock_domain.py', ANALOG / '1G/tests/hardware.py',
                 ANALOG / 'pdk/1G_repeater_IHPSG13G2/tool_process.py'):
        hashes[str(path)] = digest(path)
    report = dict(status='running', flow=args.flow, work=str(work), source_sha256=hashes,
                  scope='continuous controller/oscillator/admission circuit at 200 fF; excludes receiver clock tree, reset distribution and PVT qualification')

    def save():
        payload = json.dumps(report, indent=2) + '\n'
        (work / 'checks.json').write_text(payload)
        (args.work / 'checks.json').write_text(payload)

    def command(label, argv, env=None, timeout=1800, exclusive=None):
        with (work / (label + '.log')).open('w') as log:
            execute(list(map(str, argv)), cwd=work, env=os.environ if env is None else env,
                    stdout=log, stderr=-2, check=True, timeout=timeout, memory_exclusive=exclusive)

    save()
    try:
        if args.flow == 'cpp':
            with capture_trace(work / 'trace.csv') as capture:
                command('bench', [args.bench.resolve(), work, 'run'], timeout=14400)
            report['trace_storage'] = capture.metadata
            save()
        else:
            command('bench', [args.bench.resolve(), work, 'export'], timeout=14400)
        if args.flow != 'cpp':
            metadata = json.loads((work / 'live.json').read_text())
            if metadata['drivers'] != list(range(18)) or metadata['stop_s'] != 64e-6:
                raise ValueError('changed closed-clock host interface')
            include = headers(args.ngspice)
            hashes[str(args.ngspice.resolve())] = digest(args.ngspice.resolve())
            hashes.update({str(p): digest(p) for p in (include / 'ngspice').glob('*.h')})
            command('host-build', [args.cxx, '-std=c++17', '-O1', '-fPIC', '-shared',
                '-I' + str(ANALOG.parent / 'include'), '-I' + str(include),
                SOURCE.with_name('sim_processing_clock_delivery.cpp'), '-o', work / 'controller.so'], exclusive=False)
            original = (work / 'bench.cir').read_text()
            env = dict(os.environ, OMP_NUM_THREADS='1')
            if args.flow == 'psp':
                pdk = args.pdk.resolve()
                models = pdk / 'ihp-sg13g2/libs.tech/ngspice/models'
                hashes.update({str(p): digest(p) for p in list(models.rglob('*.lib')) + list(models.rglob('*.spice'))})
                original, report['binding'] = bind_live((work / 'design.va').read_text(), original, models, metadata)
                env.update(PDK_ROOT=str(pdk), PDK='ihp-sg13g2',
                           SPICE_USERINIT_DIR=str(pdk / 'ihp-sg13g2/libs.tech/ngspice'))
            else:
                hashes[str(args.openvaf.resolve())] = digest(args.openvaf.resolve())
                # This 89-device export fits a bounded compiler beside routing.
                # Large waveform parsing still takes the shared memory slot.
                limiter = shutil.which('prlimit')
                if limiter is None:
                    command('openvaf', [args.openvaf.resolve(), 'design.va', '-o', 'design.osdi'])
                else:
                    command('openvaf', [limiter, '--as=' + str(1 << 30), '--',
                        args.openvaf.resolve(), 'design.va', '-o', 'design.osdi'], exclusive=False)
            deck, control = original.split('.control\n', 1)
            observations = re.findall(r'^let observation(\d+) = (v\(n\d+\))$', control, re.M)
            if [int(number) for number, _ in observations] != list(range(8)):
                raise ValueError('changed closed-clock electrical observations')
            # Preserve the complete observed trajectory without retaining every
            # internal OSDI/bridge node for millions of transient steps.
            deck += '.save ' + ' '.join(node for _, node in observations) + '\n'
            analog, digital = [], []
            for identifier in metadata['drivers']:
                initial = 0 if identifier in (7, 17) else 1
                pattern = rf'^Vbit{identifier} (bit{identifier}) 0 DC {initial}$'
                match = re.search(pattern, deck, re.M)
                if not match:
                    raise ValueError('missing host control source')
                analog.append(match[1]); digital.append('host_bit' + str(identifier))
                deck, count = re.subn(pattern, '', deck, flags=re.M)
                if count != 1:
                    raise ValueError('duplicate host control source')
            deck += '\nVhost_ref host_ref 0 PULSE(0 1.2 4n 1p 1p 3.999n 8n)\n'
            deck += f'Aclocks [host_ref {metadata["clock"]}] [ref_clock pcs_clock] host_input\n'
            deck += f'Acontroller [ref_clock pcs_clock] [{" ".join(digital)}] null host_controller\n'
            deck += dac_bridge_cards(digital, analog, 'host_output', 'host_controls') + '\n'
            deck += '.model host_input adc_bridge(in_low=.36 in_high=.84 rise_delay=1p fall_delay=1p)\n'
            deck += '.model host_output dac_bridge(out_low=0 out_high=1 out_undef=.5 t_rise=100p t_fall=100p)\n'
            deck += '.model host_controller d_cosim(simulation="./controller.so" sim_args=["events.csv"] delay=1p)\n'
            (work / 'closed.cir').write_text(deck + '.control\nset num_threads=1\n' + control)
            save()
            with capture_trace(work / 'spice.dat') as capture:
                command('ngspice', ['prlimit', '--as=1073741824', '--', args.ngspice.resolve(), '-b', 'closed.cir'],
                        env, timeout=43200, exclusive=False)
            report['trace_storage'] = capture.metadata
            save()
        # Full electrical trajectories can occupy several GiB after parsing.
        # Share the EDA memory budget with routing and other waveform readers.
        with memory_slot(['processing-clock-waveform-check'], exclusive=True):
            resource.setrlimit(resource.RLIMIT_AS, (2 << 30, 2 << 30))
            trace = work / ('trace.csv.gz' if args.flow == 'cpp' else 'spice.dat.gz')
            with gzip.open(trace, 'rt') as stream:
                if args.flow == 'cpp':
                    reader = csv.reader(stream)
                    if next(reader) != HEADER:
                        raise ValueError('changed native clock observations')
                else:
                    names = next(stream).split()
                    if len(names) != len(HEADER) or names[0] != 'time':
                        raise ValueError('changed SPICE clock observations')
                    reader = (line.split() for line in stream if line.strip())
                rows = checked_rows(reader, len(HEADER), str(trace))
            with (work / ('controls.csv' if args.flow == 'cpp' else 'events.csv')).open() as stream:
                records = list(csv.DictReader(stream))
            report['electrical'] = qualify(rows, records, args.flow)
            if any(digest(Path(p)) != h for p, h in hashes.items()):
                raise ValueError('closed-clock qualification inputs changed')
            report['trace_sha256'] = report['trace_storage']['original_sha256']
        report['status'] = 'pass'
        print('PASS closed processing clock delivery: ' + args.flow)
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
