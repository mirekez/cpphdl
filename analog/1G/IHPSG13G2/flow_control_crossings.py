#!/usr/bin/env python3
"""Check extracted FIFO and PAUSE crossings independently of clock-group cuts."""
import argparse
import json
import math
from pathlib import Path
import re
import sys
import tempfile

SOURCE = Path(__file__).resolve()
ANALOG = SOURCE.parents[2]
COMMON = ANALOG / 'pdk/1G_repeater_IHPSG13G2'
sys.path[:0] = [str(SOURCE.parent), str(COMMON)]
from extract_analog import digest
from input_charge import verify_files
from tool_process import execute, configure_cleanup

# Maximum relative arrival window is 2.5 ns, below either source period
# (7.353 ns RX, 8 ns TX). This check includes source-clock insertion spread.
DATA_LIMIT_NS = 2.0
CLOCK_SPREAD_NS = 0.5
BUSES = {}
for direction, rx, tx in [('a_to_b', 'rx_clk', 'tx_clk'),
                          ('b_to_a', 'b_rx_clk', 'a_tx_clk')]:
    prefix = 'forwarding.' + direction + '.crossing.'
    for kind, source_clock, target_clock in [('write', rx, tx), ('read', tx, rx)]:
        BUSES[direction + '_' + kind] = dict(source=prefix + kind + '_gray',
            target=prefix + kind + '_sync1',
            source_clock=source_clock, target_clock=target_clock, width=13)
    BUSES[direction + '_fault'] = dict(source=prefix + 'source_fault', target=prefix + 'fault_sync1',
        source_clock=rx, target_clock=tx, width=1)

for port, direction, rx, tx in [('a', 'a_to_b', 'rx_clk', 'a_tx_clk'),
                               ('b', 'b_to_a', 'b_rx_clk', 'tx_clk')]:
    prefix = port + '.commands.'
    for kind, source, target, width, launch, capture in [
            ('payload', 'payload', '_output', 16, rx, tx),
            ('request', 'request', 'request_first', 1, rx, tx),
            ('acknowledge', 'acknowledge', 'ack_first', 1, tx, rx)]:
        BUSES[port + '_' + kind] = dict(source=prefix + source, target=prefix + target,
            width=width, source_clock=launch, target_clock=capture,
            bundle=port if kind in ('payload', 'request') else None)
    BUSES[port + '_pressure'] = dict(source='forwarding.' + direction + '.pause_request',
        target=port + '.request_first', width=1, source_clock=rx, target_clock=tx)

# Peer-reset acknowledgements cross the same two synchronizers before release.
# Data-path delay here does not establish asynchronous-reset recovery/removal.
for prefix, rx, tx in [('a.resets.', 'rx_clk', 'a_tx_clk'),
                       ('b.resets.', 'b_rx_clk', 'tx_clk'),
                       ('forwarding.a_to_b.resets.', 'rx_clk', 'tx_clk'),
                       ('forwarding.b_to_a.resets.', 'b_rx_clk', 'a_tx_clk')]:
    for source, target, launch, capture in [('rx_released', 'rx_seen1', rx, tx),
                                           ('tx_released', 'tx_seen1', tx, rx)]:
        BUSES[prefix.replace('.', '_') + source] = dict(source=prefix + source,
            target=prefix + target, width=1, source_clock=launch, target_clock=capture)


def qualify(paths):
    expected = {(bus, bit, corner, delay) for bus, spec in BUSES.items()
                for bit in range(spec['width']) for corner in ('slow', 'fast')
                for delay in ('min', 'max')}
    if set(paths) != expected:
        raise ValueError('incomplete extracted FIFO crossing matrix')
    results, bundle_clocks = {}, {}
    for bus, spec in BUSES.items():
        corners = {}
        for corner in ('slow', 'fast'):
            latencies, arrivals = [], []
            for bit in range(spec['width']):
                for delay in ('min', 'max'):
                    checks = paths[bus, bit, corner, delay].get('checks', [])
                    if len(checks) != 1:
                        raise ValueError('missing or ambiguous FIFO crossing path')
                    path = checks[0]
                    clock = path.get('source_clock_path', [])
                    if (path.get('type') != 'path_delay' or path.get('path_type') != delay or not clock or
                            path.get('source_clock') != spec['source_clock'] or
                            path.get('target_clock') != spec['target_clock'] or
                            path.get('source_clock_edge') != 'rise' or
                            path.get('target_clock_edge') != 'rise' or
                            not path.get('startpoint', '').endswith('/Q') or
                            not path.get('endpoint', '').endswith('/D')):
                        raise ValueError('unexpected FIFO crossing endpoints or clocks')
                    latency, arrival, slack = clock[-1]['arrival'], path['data_arrival_time'], path['slack']
                    if any(not isinstance(v, (float, int)) or not math.isfinite(v)
                           for v in (latency, arrival, slack)):
                        raise ValueError('nonfinite FIFO crossing timing')
                    if latency < 0 or arrival < 0 or arrival > DATA_LIMIT_NS * 1e-9:
                        raise ValueError('FIFO Gray-bus data delay exceeds 2 ns')
                    if delay == 'max' and slack < 0:
                        raise ValueError('FIFO Gray-bus maximum-delay reserve failed')
                    latencies.append(latency)
                    arrivals.append(arrival)
                    if spec.get('bundle'):
                        bundle_clocks.setdefault((spec['bundle'], corner), []).append(latency)
            spread = max(latencies) - min(latencies)
            if spread > CLOCK_SPREAD_NS * 1e-9:
                raise ValueError('FIFO Gray-bus source-clock spread exceeds 500 ps')
            corners[corner] = dict(maximum_data_delay_ns=max(arrivals) * 1e9,
                source_clock_spread_ns=spread * 1e9,
                conservative_arrival_window_ns=(max(arrivals) + spread) * 1e9)
        results[bus] = dict(bits=spec['width'], corners=corners)
    # Payload and request launch together. Account for their relative clock
    # insertion, not just skew within the sixteen payload bits.
    for (bundle, corner), values in bundle_clocks.items():
        spread = max(values) - min(values)
        if spread > CLOCK_SPREAD_NS * 1e-9:
            raise ValueError('PAUSE payload/request source-clock spread exceeds 500 ps')
        results[bundle + '_payload']['corners'][corner]['request_payload_clock_spread_ns'] = spread * 1e9
    return results


def register_bindings(netlist):
    """Resolve semantic state aliases before OpenROAD discards redundant nets."""
    module = netlist['modules']['FlowControlledRepeater']
    drivers = {}
    for name, cell in module['cells'].items():
        if re.fullmatch(r'sg13g2_dfrbpq_[12]', cell['type']):
            for bit in cell['connections']['Q']:
                drivers.setdefault(bit, []).append(name)
    result = {}
    for bus, spec in BUSES.items():
        for state in ('source', 'target'):
            bits = module['netnames'].get(spec[state], {}).get('bits', [])
            if len(bits) != spec['width']:
                raise ValueError('missing semantic FIFO state: ' + spec[state])
            for index, bit in enumerate(bits):
                names = drivers.get(bit, [])
                if len(names) != 1 or not re.fullmatch(r'[A-Za-z0-9_]+', names[0]):
                    raise ValueError('missing or ambiguous FIFO state register')
                result[bus, state, index] = names[0]
    return result


def crossing_script(work, bindings):
    text = r'''
proc state_cell {name} {
    set cell [get_cells -quiet $name]
    if {[llength $cell] != 1 || ![regexp {^sg13g2_dfrbpq_[12]$} [get_property $cell ref_name]]} {
        error "missing or changed FIFO state register: $name"
    }
    return $cell
}
'''
    for bus, spec in BUSES.items():
        for bit in range(spec['width']):
            source = bindings[bus, 'source', bit]
            target = bindings[bus, 'target', bit]
            text += f'''set source [state_cell {source}]
set target [state_cell {target}]
set endpoint [get_pins [get_name $target]/D]
set_max_delay -ignore_clock_latency -from $source -to $endpoint {DATA_LIMIT_NS}
set_min_delay -ignore_clock_latency -from $source -to $endpoint 0.0
'''
            for corner in ('slow', 'fast'):
                for delay in ('min', 'max'):
                    output = work / f'{bus}-{bit}-{corner}-{delay}.json'
                    text += f'report_checks -from $source -to $endpoint -scenes {corner} -path_delay {delay} -format json > {{{output}}}\n'
    return text


def run(args):
    proof = json.loads(args.routed.read_text())
    if proof.get('status') != 'routed_timing_replay_pass':
        raise ValueError('current routed timing and functional replay required')
    hashes = {}
    for key in ('source_sha256', 'artifact_sha256'):
        verify_files(proof[key])
        hashes.update(proof[key])
    cell_proof = json.loads(args.cells.read_text())
    if (cell_proof.get('status') != 'pass' or
            hashes.get(str(args.cells.resolve())) != digest(args.cells)):
        raise ValueError('routing used another cell mapping')
    origin = Path(proof['work'])
    libraries = args.pdk.resolve() / 'ihp-sg13g2/libs.ref'
    libfiles = []
    text = 'set_thread_count 1\ndefine_corners slow fast\n'
    for corner, std, sram in [('slow', 'slow_1p08V_125C', 'slow_1p08V_125C'),
                              ('fast', 'fast_1p32V_m40C', 'fast_1p32V_m55C')]:
        for family, cell, suffix in [('sg13g2_stdcell', 'sg13g2_stdcell', std),
                ('sg13g2_sram', 'RM_IHPSG13_2P_1024x16_c2_bm_bist', sram)]:
            library = libraries / family / 'lib' / (cell + '_' + suffix + '.lib')
            libfiles.append(library)
            text += f'read_liberty -corner {corner} {{{library}}}\n'
    args.work.mkdir(parents=True, exist_ok=True)
    work = Path(tempfile.mkdtemp(prefix='run-', dir=args.work.resolve()))
    text += f'read_db {{{origin / "routed.odb"}}}\n'
    # No read_sdc: its asynchronous clock cuts would override these constraints.
    for clock, period in [('rx_clk', 1000 / 136), ('b_rx_clk', 1000 / 136),
                          ('a_tx_clk', 8), ('tx_clk', 8)]:
        text += f'create_clock -name {clock} -period {period:.12g} [get_ports {clock}]\n'
    text += 'set_propagated_clock [all_clocks]\nset_input_transition 0.1 [all_inputs]\n'
    for corner in ('slow', 'fast'):
        text += f'read_spef -corner {corner} {{{origin / "core.spef"}}}\n'
    mapped = Path(cell_proof['work']) / 'mapped.v'
    # write_verilog renames Yosys automatic cells. Re-import that exact netlist
    # so register names match the instances consumed by place and route.
    naming_script = work / 'names.ys'
    naming_script.write_text('read_verilog ' + json.dumps(str(mapped)) + '\nwrite_json ' +
                             json.dumps(str(work / 'names.json')) + '\n')
    with (work / 'names.log').open('w') as log:
        execute(['prlimit', '--as=536870912', '--', args.yosys.resolve(), '-s', naming_script],
                stdout=log, stderr=-2, timeout=120, check=True, memory_exclusive=False)
    bindings = register_bindings(json.loads((work / 'names.json').read_text()))
    text += crossing_script(work, bindings) + 'exit\n'
    script = work / 'crossings.tcl'
    script.write_text(text)
    files = [SOURCE, SOURCE.with_name('extract_analog.py'), SOURCE.with_name('input_charge.py'),
             COMMON / 'tool_process.py', args.routed.resolve(), script, args.openroad.resolve(),
             args.yosys.resolve(), naming_script, mapped, *libfiles]
    hashes.update({str(p): digest(p) for p in files})
    report = dict(status='running', work=str(work), source_sha256=hashes,
        scope='FIFO Gray buses, PAUSE bundled data and synchronized control paths; '
              'excludes synchronizer MTBF, asynchronous-reset recovery/removal, configuration crossings and post-fill coupling')

    def save():
        payload = json.dumps(report, indent=2) + '\n'
        (work / 'checks.json').write_text(payload)
        (args.work / 'checks.json').write_text(payload)

    save()
    try:
        with (work / 'openroad.log').open('w') as log:
            execute(['prlimit', '--as=1610612736', '--', args.openroad.resolve(), '-no_init', '-exit', script],
                    stdout=log, stderr=-2, timeout=300, check=True, memory_exclusive=True)
        if '[ERROR' in (work / 'openroad.log').read_text():
            raise ValueError('FIFO crossing STA failed')
        paths = {(bus, bit, corner, delay): json.loads((work / f'{bus}-{bit}-{corner}-{delay}.json').read_text())
                 for bus, spec in BUSES.items() for bit in range(spec['width'])
                 for corner in ('slow', 'fast') for delay in ('min', 'max')}
        report['buses'] = qualify(paths)
        verify_files(hashes)
        report.update(status='crossing_data_timing_pass', artifact_sha256={str(p): digest(p)
            for p in work.glob('*.json') if p.name != 'checks.json'})
        print('PASS extracted crossing data timing on ' + str(sum(s['width'] for s in BUSES.values())) +
              ' FIFO/PAUSE/control bits at both corners')
    except BaseException as error:
        report.update(status='fail', error=str(error))
        raise
    finally:
        save()


if __name__ == '__main__':
    configure_cleanup()
    parser = argparse.ArgumentParser(description=__doc__)
    for name in ('routed', 'cells', 'work', 'pdk', 'openroad', 'yosys'):
        parser.add_argument('--' + name, type=Path, required=True)
    run(parser.parse_args())
