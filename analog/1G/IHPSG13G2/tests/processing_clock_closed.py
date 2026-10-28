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
import shutil
import sys
import tempfile

SOURCE = Path(__file__).resolve()
ANALOG = SOURCE.parents[3]
sys.path[:0] = [str(SOURCE.parent.parent), str(ANALOG / '1G/tests'), str(ANALOG / 'VerilogA')]
from processing_clock_process import process_deck
from compare import read_cpp, read_spice, edges, interpolate
from hardware import configure_cleanup, execute, source_dependencies
from tool_process import memory_slot
from reference_clock_domain import digest
from xspice_live import headers
from xspice_bridges import dac_bridge_cards

HEADER = ['time_s', 'OUT', 'RING', 'CONTROL', 'VDD', 'CLEAR']


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
    if not rows or any(len(r) != len(HEADER) or not all(math.isfinite(v) for v in r) for r in rows):
        raise ValueError('invalid electrical clock observations')
    if any(b[0] <= a[0] for a, b in zip(rows, rows[1:])):
        raise ValueError('reordered electrical clock observations')
    if not 0 <= rows[0][0] <= 50e-12 or abs(rows[-1][0] - 64e-6) > 1e-14:
        raise ValueError('closed-clock transient did not finish')
    if max(b[0] - a[0] for a, b in zip(rows, rows[1:])) > 50.05e-12:
        raise ValueError('unresolved closed-clock trajectory')
    if flow == 'cpp':
        if [int(row['reference_edge']) for row in records] != list(range(-1, 8000)):
            raise ValueError('missing native reference-clock events')
        references = records[1:]
    else:
        times = [float(row['time_s']) for row in records]
        if any(b <= a for a, b in zip(times, times[1:])):
            raise ValueError('reordered host clock events')
        references = [r for r in records if int(r['ref_edge'])]
        if len(references) != 8000:
            raise ValueError('missing delivered reference-clock edges')
        for index, row in enumerate(references):
            if abs(float(row['time_s']) - (4e-9 + index * 8e-9)) > 60e-12:
                raise ValueError('host reference clock differs from the sole external reference')
    if any(int(row['fault']) for row in records):
        raise ValueError('clock controller reported acquisition failure')
    if not all(int(r['ready_ref']) and int(r['ready_pcs']) and 543 <= int(r['measured']) <= 556
               for r in references[-512:]):
        raise ValueError('processing clock did not retain qualified readiness')
    if len({int(r['trim']) for r in references}) < 2:
        raise ValueError('controller never changed the physical oscillator trim')
    crossings = edges(rows, 1, .6, 60e-6, 64e-6)
    if len(crossings) < 500:
        raise ValueError('delivered processing clock stopped')
    frequency = (len(crossings) - 1) / (crossings[-1] - crossings[0])
    times = [r[0] for r in rows]
    high = [interpolate(rows, times, a + (b - a) / 4, 1) for a, b in zip(crossings, crossings[1:])]
    low = [interpolate(rows, times, a + 3 * (b - a) / 4, 1) for a, b in zip(crossings, crossings[1:])]
    late = [r for r in rows if r[0] >= 60e-6]
    if not (132e6 <= frequency <= 136e6 and min(high) > 1.08 and max(low) < .12 and
            min(r[4] for r in late) > 1.1 and all(-.1 < r[c] < 1.3 for r in late for c in (1, 2, 3))):
        raise ValueError('qualified controller output fails independent electrical requirements')
    return dict(frequency_Hz=frequency, high_min_V=min(high), low_max_V=max(low),
                supply_min_V=min(r[4] for r in late), final_trim=int(references[-1]['trim']),
                reference_edges=len(references), sustained_qualified_reference_edges=512)


def run(args):
    args.work.mkdir(parents=True, exist_ok=True)
    work = Path(tempfile.mkdtemp(prefix='run-', dir=args.work.resolve()))
    hashes = source_dependencies([SOURCE.with_suffix('.cpp')], ANALOG.parent, args.cxx,
        ['-std=c++20', '-I' + str(ANALOG), '-I' + str(ANALOG / 'include'), '-I/usr/include/eigen3'])
    for path in (SOURCE, SOURCE.with_name('sim_processing_clock.cpp'), args.bench.resolve(),
                 SOURCE.parent.parent / 'processing_clock_process.py', SOURCE.parent.parent / 'core_driver_process.py',
                 SOURCE.parent.parent / 'mos_geometry.py', SOURCE.parent.parent / 'xspice_live.py',
                 SOURCE.parent.parent / 'xspice_bridges.py', ANALOG / 'VerilogA/compare.py',
                 ANALOG / '1G/tests/reference_clock_domain.py', ANALOG / '1G/tests/hardware.py',
                 ANALOG / 'pdk/1G_repeater_IHPSG13G2/tool_process.py'):
        hashes[str(path)] = digest(path)
    report = dict(status='running', flow=args.flow, work=str(work), source_sha256=hashes,
                  scope='continuous electrical oscillator/controller acquisition; excludes extracted clock-tree and PVT qualification')

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
        command('bench', [args.bench.resolve(), work, 'run' if args.flow == 'cpp' else 'export'], timeout=14400)
        if args.flow != 'cpp':
            metadata = json.loads((work / 'live.json').read_text())
            if metadata['drivers'] != list(range(17)) or metadata['stop_s'] != 64e-6:
                raise ValueError('changed closed-clock host interface')
            include = headers(args.ngspice)
            hashes[str(args.ngspice.resolve())] = digest(args.ngspice.resolve())
            hashes.update({str(p): digest(p) for p in (include / 'ngspice').glob('*.h')})
            command('host-build', [args.cxx, '-std=c++17', '-O1', '-fPIC', '-shared',
                '-I' + str(ANALOG.parent / 'include'), '-I' + str(include),
                SOURCE.with_name('sim_processing_clock.cpp'), '-o', work / 'controller.so'], exclusive=False)
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
                command('openvaf', [args.openvaf.resolve(), 'design.va', '-o', 'design.osdi'])
            deck, control = original.split('.control\n', 1)
            observations = re.findall(r'^let observation(\d+) = (v\(n\d+\))$', control, re.M)
            if [int(number) for number, _ in observations] != list(range(5)):
                raise ValueError('changed closed-clock electrical observations')
            # Preserve the complete observed trajectory without retaining every
            # internal OSDI/bridge node for millions of transient steps.
            deck += '.save ' + ' '.join(node for _, node in observations) + '\n'
            analog, digital = [], []
            for identifier in metadata['drivers']:
                initial = 0 if identifier == 7 else 1
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
            command('ngspice', [args.ngspice.resolve(), '-b', 'closed.cir'], env, timeout=43200)
        # Full electrical trajectories can occupy several GiB after parsing.
        # Share the EDA memory budget with routing and other waveform readers.
        with memory_slot(['processing-clock-waveform-check'], exclusive=True):
            trace = work / ('trace.csv' if args.flow == 'cpp' else 'spice.dat')
            if args.flow == 'cpp':
                header, rows = read_cpp(trace)
                if header != HEADER:
                    raise ValueError('changed native clock observations')
            else:
                rows = read_spice(trace, HEADER)
            with (work / ('controls.csv' if args.flow == 'cpp' else 'events.csv')).open() as stream:
                records = list(csv.DictReader(stream))
            report['electrical'] = qualify(rows, records, args.flow)
            if any(digest(Path(p)) != h for p, h in hashes.items()):
                raise ValueError('closed-clock qualification inputs changed')
            report['trace_sha256'] = digest(trace)
            with trace.open('rb') as source, gzip.open(str(trace) + '.gz', 'wb') as target:
                shutil.copyfileobj(source, target)
            trace.unlink()
        report['status'] = 'pass'
        print('PASS closed processing clock: ' + args.flow)
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
