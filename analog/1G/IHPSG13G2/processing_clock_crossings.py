#!/usr/bin/env python3
"""Check extracted Gray-bus delay bounds separately from asynchronous clock cuts."""
import argparse
import json
import math
from pathlib import Path
import subprocess
import tempfile

SOURCE = Path(__file__).resolve()
ANALOG = SOURCE.parents[2]
BUSES = {'count': ('gray', 'count_sync1', 14),
         'reference': ('reference_gray', 'reference_sync1', 8)}


def qualify(paths):
    expected = {(bus, bit, corner, delay) for bus, (_, _, width) in BUSES.items()
                for bit in range(width) for corner in ('slow', 'fast') for delay in ('min', 'max')}
    if set(paths) != expected:
        raise ValueError('incomplete extracted crossing matrix')
    result = {}
    for bus, (source, target, width) in BUSES.items():
        clocks = {corner: [] for corner in ('slow', 'fast')}
        data = {corner: [] for corner in ('slow', 'fast')}
        for key in sorted(k for k in expected if k[0] == bus):
            checks = paths[key].get('checks', [])
            if len(checks) != 1:
                raise ValueError('missing or ambiguous crossing path')
            path = checks[0]
            clock = path.get('source_clock_path', [])
            points = path.get('source_path', [])
            if (not clock or not points or path.get('type') != 'path_delay' or
                    path.get('source_clock') != ('acquisition' if bus == 'count' else 'reference') or
                    path.get('target_clock') != ('reference' if bus == 'count' else 'acquisition') or
                    path.get('source_clock_edge') != 'rise' or
                    not path.get('startpoint', '').endswith('/Q') or
                    not path.get('endpoint', '').endswith('/D')):
                raise ValueError('unexpected extracted crossing endpoints')
            values = (clock[-1]['arrival'], path['data_arrival_time'], path['slack'])
            if any(not isinstance(v, (int, float)) or not math.isfinite(v) for v in values):
                raise ValueError('nonfinite extracted crossing timing')
            latency, arrival, slack = values
            if latency < 0 or arrival < 0 or arrival > 1e-9:
                raise ValueError('Gray-bus data delay exceeds 1 ns')
            if key[3] == 'max' and slack < 0:
                raise ValueError('Gray-bus maximum delay/setup reserve failed')
            clocks[key[2]].append(latency)
            data[key[2]].append(arrival)
        # Clock insertion at slow and fast PVT is a common phase shift, not
        # bit-to-bit skew within one operating condition. Check each corner.
        corners = {}
        for corner in ('slow', 'fast'):
            spread = max(clocks[corner]) - min(clocks[corner])
            if spread > .25e-9:
                raise ValueError('Gray-bus source-clock spread exceeds 250 ps')
            corners[corner] = dict(maximum_data_delay_ns=max(data[corner]) * 1e9,
                                   source_clock_spread_ns=spread * 1e9,
                                   conservative_arrival_window_ns=(max(data[corner]) + spread) * 1e9)
        # Even the conservative zero minimum data delay leaves the entire bus
        # arrival window below the fastest source period (1.25 ns).
        result[bus] = dict(bits=width, corners=corners, conservative_arrival_window_ns=
                           max(value['conservative_arrival_window_ns'] for value in corners.values()))
    return result


def run(args):
    from physical_module import changed_inputs, digest
    physical = args.implementation.resolve()
    evidence_path = physical / 'physical_module.json'
    evidence_bytes = evidence_path.read_bytes()
    evidence = json.loads(evidence_bytes)
    if (evidence.get('top') != 'ProcessingClockControl' or
            not evidence.get('native_postroute_exact') or
            not evidence.get('input_sha256') or
            changed_inputs(evidence['input_sha256'], evidence.get('retained_input_paths'))):
        raise ValueError('fresh, exactly replayed routed controller required')
    artifacts = [physical / name for name in ('postroute.v', 'core.spef')]
    if any(evidence.get('artifact_sha256', {}).get(p.name) != digest(p) for p in artifacts):
        raise ValueError('untracked or changed routed controller')
    traces = [physical / name for name in ('cpp_trace.csv', 'postroute_trace.csv')]
    if digest(traces[0]) != digest(traces[1]):
        raise ValueError('routed controller replay differs from its native trace')
    work = args.work.resolve()
    work.mkdir(parents=True, exist_ok=True)
    if any(work.iterdir()):
        raise ValueError('crossing work directory must be fresh')
    # DRC/LVS can finish after this independent timing audit. Retain the exact
    # receipt consumed here; later status updates cannot change its inputs.
    snapshot = work / 'implementation.json'
    snapshot.write_bytes(evidence_bytes)
    library = args.pdk.resolve() / 'ihp-sg13g2/libs.ref/sg13g2_stdcell'
    slow = library / 'lib/sg13g2_stdcell_slow_1p08V_125C.lib'
    fast = library / 'lib/sg13g2_stdcell_fast_1p32V_m40C.lib'
    lef = [library / ('lef/' + name) for name in ('sg13g2_tech.lef', 'sg13g2_stdcell.lef')]
    inputs = [SOURCE, SOURCE.with_name('physical_module.py'), snapshot,
              *artifacts, *traces, slow, fast, *lef, args.openroad.resolve()]
    if any(any(c in str(p) for c in '{}\n') for p in [work, *inputs]):
        raise ValueError('unsupported Tcl path')
    script = rf'''set_thread_count 1
read_lef {{{lef[0]}}}
read_lef {{{lef[1]}}}
define_corners slow fast
read_liberty -corner slow {{{slow}}}
read_liberty -corner fast {{{fast}}}
read_verilog {{{artifacts[0]}}}
link_design ProcessingClockControl
create_clock -name reference -period 8 [get_ports ref_clk]
create_clock -name acquisition -period 1.25 [get_ports pcs_clk]
set_input_transition 0.1 [all_inputs]
set_propagated_clock [all_clocks]
read_spef -corner slow {{{artifacts[1]}}}
read_spef -corner fast {{{artifacts[1]}}}
# Deliberately no asynchronous clock-group cut in this separate analysis.
# Such cuts override maximum-delay exceptions and would hide these paths.
proc register_driver {{name bit}} {{
    set net [get_nets -quiet -regexp [format {{^%s\[%d\]$}} $name $bit]]
    if {{[llength $net] == 0 && $name == "reference_gray" && $bit == 7}} {{
        # The binary and Gray MSB are identical; synthesis may keep this alias.
        set net [get_nets -regexp {{^reference_count\[7\]$}}]
    }}
    if {{[llength $net] != 1}} {{error "missing Gray-bus register net"}}
    set pins [get_pins -of_objects $net -filter {{direction == output}}]
    if {{[llength $pins] != 1}} {{error "ambiguous Gray-bus register driver"}}
    set cell [get_cells -of_objects $pins]
    if {{![regexp {{^sg13g2_dfrbpq_[12]$}} [get_property $cell ref_name]]}} {{
        error "Gray-bus driver is not the expected flip-flop"
    }}
    return $cell
}}
'''
    for bus, (source, target, width) in BUSES.items():
        for bit in range(width):
            script += f'''set source [register_driver {source} {bit}]
set sink [register_driver {target} {bit}]
set endpoint [get_pins [get_name $sink]/D]
set_max_delay -ignore_clock_latency -from $source -to $endpoint 1.0
set_min_delay -ignore_clock_latency -from $source -to $endpoint 0.0
'''
            for corner in ('slow', 'fast'):
                for delay in ('min', 'max'):
                    output = work / f'{bus}-{bit}-{corner}-{delay}.json'
                    script += f'report_checks -from $source -to $endpoint -scenes {corner} -path_delay {delay} -format json > {{{output}}}\n'
    script += 'exit\n'
    script_path = work / 'crossings.tcl'
    script_path.write_text(script)
    inputs.append(script_path)
    hashes = {str(p): digest(p) for p in inputs}
    report = dict(status='running', input_sha256=hashes, implementation=str(evidence_path),
                  scope='extracted Gray-bus data delay and source-clock spread only; '
                        'excludes reset release, metastability MTBF, control crossings, on-chip variation and post-fill coupling')
    try:
        with (work / 'openroad.log').open('w') as log:
            result = subprocess.run(['/usr/bin/prlimit', '--as=1073741824', '--',
                                     str(args.openroad.resolve()), '-no_init', '-exit', str(script_path)],
                                    stdout=log, stderr=-2, timeout=120)
        if result.returncode or '[ERROR' in (work / 'openroad.log').read_text():
            raise ValueError('extracted crossing analysis failed')
        paths = {}
        for bus, (_, _, width) in BUSES.items():
            for bit in range(width):
                for corner in ('slow', 'fast'):
                    for delay in ('min', 'max'):
                        paths[bus, bit, corner, delay] = json.loads(
                            (work / f'{bus}-{bit}-{corner}-{delay}.json').read_text())
        report['buses'] = qualify(paths)
        if (any(digest(Path(p)) != h for p, h in hashes.items()) or
                changed_inputs(evidence['input_sha256'], evidence.get('retained_input_paths'))):
            raise ValueError('crossing inputs changed during analysis')
        report['artifact_sha256'] = {p.name: digest(p) for p in work.glob('*.json')}
        report['status'] = 'pass'
    except BaseException as error:
        report.update(status='fail', error=str(error))
        raise
    finally:
        (work / 'checks.json').write_text(json.dumps(report, indent=2) + '\n')
    print(json.dumps(report['buses'], indent=2))


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--implementation', type=Path, required=True)
    destination = parser.add_mutually_exclusive_group(required=True)
    destination.add_argument('--work', type=Path)
    destination.add_argument('--work-root', type=Path)
    parser.add_argument('--pdk', type=Path, default=ANALOG / 'pdk/IHPSG13G2')
    parser.add_argument('--openroad', type=Path, default=ANALOG / 'out/repeater-toolchain/bin/openroad')
    args = parser.parse_args()
    if args.work_root:
        args.work_root.mkdir(parents=True, exist_ok=True)
        args.work = Path(tempfile.mkdtemp(prefix='run-', dir=args.work_root.resolve()))
    run(args)
