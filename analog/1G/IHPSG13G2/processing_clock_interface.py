#!/usr/bin/env python3
"""Connect the routed controller's admission cells to the physical MOS latch."""
import argparse
import gzip
import json
import os
from pathlib import Path
import re
import resource
import shutil
import sys
import tempfile

SOURCE = Path(__file__).resolve()
ANALOG = SOURCE.parents[2]
sys.path[:0] = [str(SOURCE.parent / 'tests'), str(ANALOG / 'VerilogA')]
from processing_clock_gate import HEADER, STOP, qualify, crossings, clock_schedule, interpolate
from processing_clock_gate_process import process_deck
from mos_geometry import size_mos_instances
from hardware import source_dependencies, execute, configure_cleanup
from input_charge import digest, verify_files
from compact_trace import read_spice


def output_cells(netlist, cdl):
    """Use actual routed masters and named pin connections, never inferred sizes."""
    expected = {
        'admission_allow': ('sg13g2_and2_1', {'A': 'enable_in', 'X': 'admission_enabled'}),
        'admission_nand': ('sg13g2_nand2_2', {'A': 'qualified_ready', 'B': 'admission_enabled', 'Y': 'admission_inverted'}),
        'admission_driver': ('sg13g2_inv_8', {'A': 'admission_inverted', 'Y': 'ready_pcs_out'}),
    }
    definitions, instances, inventory = [], [], []
    for name, (wanted, ports) in expected.items():
        matches = re.findall(r'\b(sg13g2_\w+)\s+' + name + r'\s*\(([^;]+)\);', netlist)
        if len(matches) != 1 or matches[0][0] != wanted:
            raise ValueError('changed routed admission cell: ' + name)
        pins = dict(re.findall(r'\.(\w+)\((\w+)\)', matches[0][1]))
        if name == 'admission_allow':
            if not pins.get('B'):
                raise ValueError('missing local reference-readiness input')
            ports = dict(ports, B=pins['B'])
        if pins != ports:
            raise ValueError('changed routed admission topology: ' + name)
        matches = re.findall(r'^\.SUBCKT ' + wanted + r' ([^\n]+)\n(.*?)^\.ENDS[^\n]*$', cdl, re.M | re.S)
        if len(matches) != 1:
            raise ValueError('missing or ambiguous PDK cell: ' + wanted)
        terminals, body = matches[0]
        lines = []
        for line in body.splitlines():
            if not line or line.startswith('*'):
                continue
            if not re.fullmatch(r'M\w+ \S+ \S+ \S+ \S+ sg13_lv_[np]mos m=1 w=\S+ l=\S+ ng=\d+', line):
                raise ValueError('unsupported PDK transistor declaration: ' + line)
            # Public ngspice models are PSP subcircuits. Preserve total width,
            # finger count and every terminal from the PDK CDL.
            lines.append('X' + line[1:])
        bound, geometry = size_mos_instances('\n'.join(lines))
        definitions.append('.SUBCKT ' + wanted + ' ' + terminals + '\n' + bound + '.ENDS ' + wanted)
        mapped = dict(pins, VDD='VDD', VSS='0')
        if name == 'admission_allow':
            mapped.update(A='VDD', B='VDD')
        mapped = {k: {'qualified_ready': 'COMMAND', 'ready_pcs_out': 'ADMIT'}.get(v, v)
                  for k, v in mapped.items()}
        if set(terminals.split()) != set(mapped):
            raise ValueError('changed PDK cell ports')
        instances.append('X' + name + ' ' + ' '.join(mapped[k] for k in terminals.split()) + ' ' + wanted)
        inventory.append(dict(instance=name, master=wanted, routed_pins=pins, geometry=geometry))
    return '\n'.join(definitions + instances) + '\n', inventory


def launch_cells(netlist, cdl):
    """Retain the readiness register and its dedicated raw-clock branch."""
    instances = re.findall(r'\b(sg13g2_\w+)\s+(\w+)\s*\(([^;]+)\);', netlist)
    drivers = [(master, name, dict(re.findall(r'\.(\w+)\((\w+)\)', ports)))
               for master, name, ports in instances if '.Q(qualified_ready)' in ports]
    if len(drivers) != 1 or drivers[0][0] != 'sg13g2_dfrbpq_1':
        raise ValueError('changed readiness launch register')
    master, name, pins = drivers[0]
    if (set(pins) != {'CLK', 'D', 'RESET_B', 'Q'} or pins['CLK'] != 'readiness_clock'
            or pins['Q'] != 'qualified_ready'):
        raise ValueError('changed readiness register clock or ports')
    clocks = [(m, dict(re.findall(r'\.(\w+)\((\w+)\)', ports)))
              for m, n, ports in instances if n == 'clkbuf_readiness']
    if clocks != [('sg13g2_buf_8', {'A': 'pcs_clk', 'X': 'readiness_clock'})]:
        raise ValueError('changed dedicated readiness clock branch')
    definitions, inventory = [], []
    for cell, ports, count in [('sg13g2_buf_8', 'X A VDD VSS', 4),
                              ('sg13g2_dfrbpq_1', 'Q CLK D RESET_B VDD VSS', 32)]:
        matches = re.findall(r'^\.SUBCKT ' + cell + r' ([^\n]+)\n(.*?)^\.ENDS[^\n]*$', cdl, re.M | re.S)
        if len(matches) != 1 or matches[0][0] != ports:
            raise ValueError('changed launch-cell library interface')
        lines = []
        for line in matches[0][1].splitlines():
            if not line or line.startswith('*'):
                continue
            if not re.fullmatch(r'M\w+ \S+ \S+ \S+ \S+ sg13_lv_[np]mos m=1 w=\S+ l=\S+ ng=\d+', line):
                raise ValueError('unsupported launch-cell transistor')
            lines.append('X' + line[1:])
        bound, geometry = size_mos_instances('\n'.join(lines))
        if len(geometry) != count:
            raise ValueError('changed launch-cell transistor inventory')
        definitions.append('.SUBCKT ' + cell + ' ' + ports + '\n' + bound + '.ENDS ' + cell)
        inventory.append(dict(master=cell, geometry=geometry))
    definitions += ['Xreadiness_clock launch_clock CLOCK VDD 0 sg13g2_buf_8',
                    'Xreadiness_launch qualified_launch launch_clock COMMAND LAUNCH_RESET_B VDD 0 sg13g2_dfrbpq_1']
    return '\n'.join(definitions) + '\n', dict(register=name, routed_pins=pins, cells=inventory)


def connect(deck, cells, registered=False, supply_V=None):
    observations = dict(re.findall(r'^let observation(\d+) = v\((n\d+)\)$', deck, re.M))
    if set(observations) != {'0', '1', '2', '3', '4'}:
        raise ValueError('changed admission experiment interface')
    pin, supply = observations['1'], observations['4']
    drivers = re.findall(r'^(Rphysical\d+) (n\d+) ' + pin + r' 50$', deck, re.M)
    if len(drivers) != 1 or deck.count('.control\n') != 1:
        raise ValueError('missing finite admission source')
    resistor, command = drivers[0]
    # Keep the bench's finite 50-ohm input source, now feeding the NAND's
    # qualifier terminal. The routed inverter drives the actual latch input.
    deck = re.sub(r'^' + resistor + r' .*$', resistor + ' ' + command + ' qualifier_command 50', deck, flags=re.M)
    cells = re.sub(r'\bVDD\b', supply, cells)
    cells = re.sub(r'\bCOMMAND\b', 'qualifier_command', cells)
    cells = re.sub(r'\bADMIT\b', pin, cells)
    if registered:
        if supply_V not in (1.08, 1.2, 1.32):
            raise ValueError('missing launch-register supply')
        cells = re.sub(r'\bCLOCK\b', observations['0'], cells)
        cells = re.sub(r'\bLAUNCH_RESET_B\b', 'launch_reset_b', cells)
        cells += f'Vlaunch_reset launch_reset_b 0 PWL(0 0 8n 0 8.05n {supply_V})\n'
    # Characterized 52.790 fF gate charge plus 27 fF reserved interconnect.
    return deck.replace('.control\n', cells + f'Cinterface {pin} 0 27f\n.control\n', 1)


def qualify_handoff(rows, supply):
    """A launch on edge N must wait for the complete pulse on edge N+1."""
    if (not 0 <= rows[0][0] <= 10e-12 or abs(rows[-1][0] - STOP) > 1e-14
            or any(not 0 < b[0] - a[0] <= 10.1e-12 for a, b in zip(rows, rows[1:]))):
        raise ValueError('incomplete registered admission waveform')
    raw = clock_schedule()
    for rising in (True, False):
        observed = crossings(rows, 1, supply / 2, rising)
        expected = [pulse[0 if rising else 1] for pulse in raw]
        if len(observed) != 96 or any(abs(a - b) > 100e-12 for a, b in zip(observed, expected)):
            raise ValueError('changed registered admission raw clock')
    # The existing D waveform has two 16-cycle requests. The real launch FF
    # samples them on 21/69, then the low-phase latch first admits 22/70.
    expected = [pulse for i, pulse in enumerate(raw) if 22 <= i <= 37 or 70 <= i <= 85]
    rising, falling = [crossings(rows, 3, supply / 2, direction) for direction in (True, False)]
    if len(rising) != 32 or len(falling) != 32:
        raise ValueError('registered handoff produced missing, extra or cut pulses')
    delays = []
    for rise, fall, (begin, end) in zip(rising, falling, expected):
        delays += [rise - begin, fall - end]
        if not 3e-9 <= fall - rise <= 4.1e-9:
            raise ValueError('registered handoff produced a runt or stretched pulse')
    if not all(-20e-12 <= delay <= 400e-12 for delay in delays):
        raise ValueError('admitted the launch edge instead of the following complete pulse')
    times = [row[0] for row in rows]
    levels = [interpolate(rows, times, (a + b) / 2, 3) for a, b in zip(rising, falling)]
    late = rows[next(i for i, row in enumerate(rows) if row[0] >= 8e-9):]
    if (min(levels) < .9 * supply or min(row[5] for row in late) < .95 * supply
            or any(not -.1 <= row[c] <= supply + .1 for row in late for c in range(1, 6))):
        raise ValueError('registered handoff fails output/supply rails')
    return dict(pulses=32, blocked_acquisition_cycles=32, launch_edges_blocked=2,
                minimum_high_s=min(b - a for a, b in zip(rising, falling)),
                maximum_edge_delay_s=max(delays), minimum_high_V=min(levels))


def run(args):
    physical = args.physical.resolve()
    receipt = physical / 'physical_module.json'
    proof = json.loads(receipt.read_text())
    if (proof.get('status') != 'component_checks_pass' or proof.get('top') != 'ProcessingClockControl'
            or proof.get('constraints', {}).get('ready_output_load_pf') != .08):
        raise ValueError('requires the qualified loaded clock-controller macro')
    netlist = physical / 'postroute.v'
    if proof['artifact_sha256'].get('postroute.v') != digest(netlist):
        raise ValueError('routed controller netlist changed')
    cdl = args.pdk.resolve() / 'ihp-sg13g2/libs.ref/sg13g2_stdcell/cdl/sg13g2_stdcell.cdl'
    if proof['input_sha256'].get(str(cdl)) != digest(cdl):
        raise ValueError('PDK cell definitions differ from the physical proof')
    cells, inventory = output_cells(netlist.read_text(), cdl.read_text())
    launch = None
    if args.registered:
        extra, launch = launch_cells(netlist.read_text(), cdl.read_text())
        cells = cells.replace('COMMAND', 'qualified_launch') + extra
    args.work.mkdir(parents=True, exist_ok=True)
    work = Path(tempfile.mkdtemp(prefix='run-', dir=args.work.resolve()))
    hashes = source_dependencies([SOURCE.parent / 'tests/processing_clock_gate.cpp'], ANALOG.parent, args.cxx,
        ['-std=c++20', '-I' + str(ANALOG), '-I' + str(ANALOG / 'include'), '-I/usr/include/eigen3'])
    models = args.pdk.resolve() / 'ihp-sg13g2/libs.tech/ngspice/models'
    inputs = [SOURCE, receipt, netlist, cdl, args.bench.resolve(), args.ngspice.resolve(),
        SOURCE.with_name('processing_clock_gate_process.py'), SOURCE.with_name('core_driver_process.py'),
        SOURCE.with_name('mos_geometry.py'), SOURCE.with_name('input_charge.py'),
        SOURCE.parent / 'tests/processing_clock_gate.py', ANALOG / 'VerilogA/compact_trace.py',
        ANALOG / 'VerilogA/compare.py', ANALOG / '1G/tests/hardware.py',
        ANALOG / 'pdk/1G_repeater_IHPSG13G2/tool_process.py']
    inputs += list(models.rglob('*.lib')) + list(models.rglob('*.spice'))
    hashes.update({str(p): digest(p) for p in inputs})
    report = dict(status='running', work=str(work), source_sha256=hashes, cells=inventory, cases=[],
        interconnect_capacitance_F=27e-15, registered_launch=launch,
        scope=('routed readiness-register/clock-buffer/output-cell topology driving schematic admission gate'
               if args.registered else 'routed output-cell topology driving schematic admission gate; local qualifier transitions') +
              '; excludes distributed wire RC, upstream logic, aperture sweep, metastability and full clock tree')
    def save():
        text = json.dumps(report, indent=2) + '\n'
        (work / 'checks.json').write_text(text)
        (args.work / 'checks.json').write_text(text)
    save()
    try:
        export = work / 'export'
        execute([str(args.bench.resolve()), str(export), 'export'], check=True, timeout=30)
        for corner, temperature, supply in (('mos_tt', 27, 1.2), ('mos_ss', 125, 1.08), ('mos_ff', -40, 1.32)):
            case = work / corner
            case.mkdir()
            deck, binding = process_deck((export / 'design.va').read_text(), (export / 'bench.cir').read_text(),
                                        models, corner, temperature, supply)
            (case / 'bench.cir').write_text(connect(deck, cells, args.registered, supply))
            env = dict(os.environ, PDK_ROOT=str(args.pdk.resolve()), PDK='ihp-sg13g2',
                SPICE_USERINIT_DIR=str(args.pdk.resolve() / 'ihp-sg13g2/libs.tech/ngspice'), OMP_NUM_THREADS='1')
            with (case / 'ngspice.log').open('w') as log:
                execute([str(args.ngspice.resolve()), '-b', 'bench.cir'], cwd=case, env=env,
                    stdout=log, stderr=-2, timeout=600, check=True, memory_exclusive=False)
            trace = case / 'spice.dat'
            rows = read_spice(trace, HEADER)
            metrics = qualify_handoff(rows, supply) if args.registered else qualify(rows, supply)
            # Bound actual output slew using 20/80-percent crossings in both
            # directions. The threshold matches the IHP timing-library view.
            slews = []
            for rising in (True, False):
                a, b = (.2, .8) if rising else (.8, .2)
                begin, end = crossings(rows, 2, a * supply, rising), crossings(rows, 2, b * supply, rising)
                if not begin or len(begin) != len(end):
                    raise ValueError('incomplete admission output transitions')
                slews += [y - x for x, y in zip(begin, end)]
            if not all(0 < t <= .5e-9 for t in slews):
                raise ValueError('admission driver exceeds 500 ps transition budget')
            report['cases'].append(dict(corner=corner, supply_V=supply, temperature_C=temperature,
                electrical=metrics, maximum_transition_s=max(slews), binding=binding,
                deck_sha256=digest(case / 'bench.cir'), trace_sha256=digest(trace)))
            with trace.open('rb') as source, gzip.open(str(trace) + '.gz', 'wb', compresslevel=1) as target:
                shutil.copyfileobj(source, target)
            trace.unlink()
            save()
        verify_files(hashes)
        report['status'] = 'pass'
        print('PASS routed admission ' + ('registered handoff' if args.registered else 'driver / MOS latch') + ': all three PSP conditions')
    except BaseException as error:
        report.update(status='fail', error=str(error))
        raise
    finally:
        save()


if __name__ == '__main__':
    configure_cleanup()
    resource.setrlimit(resource.RLIMIT_AS, (1 << 30, 1 << 30))
    parser = argparse.ArgumentParser(description=__doc__)
    for name in ('physical', 'pdk', 'bench', 'ngspice', 'work'):
        parser.add_argument('--' + name, type=Path, required=True)
    parser.add_argument('--cxx', default='/usr/bin/c++')
    parser.add_argument('--registered', action='store_true', help='launch through the actual readiness register and clock buffer')
    run(parser.parse_args())
