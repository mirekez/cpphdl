#!/usr/bin/env python3
"""Report routed receive controls and return paths for analog timing integration."""
import argparse
import hashlib
import json
import math
from pathlib import Path
import re
import sys

SOURCE = Path(__file__).resolve()
sys.path.insert(0, str(SOURCE.parent))
sys.path.insert(0, str(SOURCE.parents[2] / 'pdk/1G_repeater_IHPSG13G2'))
from tool_process import configure_cleanup, execute


def digest(path):
    return hashlib.sha256(Path(path).read_bytes()).hexdigest()


def interface_ports(netlist, wrapper):
    """Resolve packed physical bits through the checked public array adapter."""
    result = {}
    for name, width in (('trial_out', 8), ('acquire_out', 1), ('convert_out', 1)):
        ranges = re.findall(r'\boutput\s+(?:wire\s+)?\[(\d+):(\d+)\]\s+' + name + r'\s*;', netlist)
        connections = re.findall(r'\.' + name + r'\s*\(\s*\{([^}]+)\}\s*\)', wrapper)
        if len(ranges) != 1 or len(connections) != 1:
            raise ValueError('missing or ambiguous physical array interface: ' + name)
        left, right = map(int, ranges[0])
        physical = list(range(left, right + (1 if right >= left else -1), 1 if right >= left else -1))
        terms = [term.strip() for term in connections[0].split(',')]
        elements = []
        for term in terms:
            match = re.fullmatch(name + r'\[(\d+)\]', term)
            if not match:
                raise ValueError('unsupported public array expression: ' + term)
            elements.append(int(match[1]))
        if sorted(elements) != list(range(16)) or sorted(physical) != list(range(16 * width)):
            raise ValueError('incomplete physical array interface: ' + name)
        public = [(element, bit) for element in elements for bit in reversed(range(width))]
        for index, (element, bit) in zip(physical, public):
            label = f'{name}[{element}]' + (f'[{bit}]' if width > 1 else '')
            result[f'{name}[{index}]'] = dict(public_port=label, unused_compatibility=element >= 13)
    for index in range(16):
        name = f'comparisons_in[{index}]'
        result[name] = dict(public_port=name, unused_compatibility=index >= 13)
    for name in ('clear_out', 'evaluate_enable_out'):
        result[name] = dict(public_port=name, unused_compatibility=False)
    return result


def slew_thresholds(text):
    """Keep Liberty's measurement interval distinct from a full PWL edge."""
    result = {}
    for name in ('slew_derate_from_library', 'slew_lower_threshold_pct_rise',
            'slew_upper_threshold_pct_rise', 'slew_lower_threshold_pct_fall',
            'slew_upper_threshold_pct_fall', 'input_threshold_pct_rise',
            'input_threshold_pct_fall', 'output_threshold_pct_rise', 'output_threshold_pct_fall'):
        values = re.findall(r'^\s*' + name + r'\s*:\s*([^;]+);', text, re.M)
        if len(values) != 1 or not math.isfinite(float(values[0])):
            raise ValueError('ambiguous or missing Liberty threshold: ' + name)
        result[name] = float(values[0])
    if result['slew_derate_from_library'] != 1:
        raise ValueError('unsupported Liberty slew derating')
    for edge in ('rise', 'fall'):
        low, high = (result[f'slew_{bound}_threshold_pct_{edge}'] for bound in ('lower', 'upper'))
        if not 0 <= low < high <= 100:
            raise ValueError('invalid Liberty slew measurement interval')
        if any(not 0 < result[f'{side}_threshold_pct_{edge}'] < 100 for side in ('input', 'output')):
            raise ValueError('invalid Liberty delay measurement threshold')
    return result


def read_path(text, port=None):
    """Keep root-clock arrival separate from local flop-to-boundary delay."""
    if not text.strip() or text.strip() == 'No paths found.':
        return None
    if len(re.findall(r'^Startpoint:', text, re.M)) != 1:
        raise ValueError('expected exactly one constrained interface path')
    number = r'[-+]?\d+(?:\.\d+)?'
    fields = {}
    for label in ('Startpoint', 'Endpoint', 'Path Type', 'Corner'):
        match = re.search(r'^' + label + r': (.+)$', text, re.M)
        if not match:
            raise ValueError('incomplete interface timing report: ' + label)
        fields[label.lower().replace(' ', '_')] = match[1]
    arrival = re.search(r'^\s*(' + number + r')\s+data arrival time$', text, re.M)
    required = re.search(r'^\s*(' + number + r')\s+data required time$', text, re.M)
    slack = re.search(r'^\s*(' + number + r')\s+slack \((MET|VIOLATED)\)$', text, re.M)
    if not all((arrival, required, slack)):
        raise ValueError('missing constrained interface timing quantities')
    if slack[2] != 'MET' or slack[1].startswith('-'):
        raise ValueError('interface path violates its original timing constraint')
    before_data = text[:arrival.start()]
    has_slew = bool(re.search(r'^\s*Slew\s+Delay\s+Time\s+Description$', text, re.M))
    clock_columns = (number + r'\s+') * (2 if has_slew else 1)
    clocks = re.findall(r'^\s*' + clock_columns + '(' + number + r')\s+[\^v]\s+(\S+/CLK)\s+\(', before_data, re.M)
    fields.update(arrival_from_root_clock_ns=float(arrival[1]), required_from_root_clock_ns=float(required[1]),
        slack_ns=float(slack[1]))
    if clocks:
        clock_time, clock_pin = clocks[-1]
        fields.update(launch_clock_pin=clock_pin, launch_clock_arrival_ns=float(clock_time),
            launch_clock_to_boundary_ns=float(arrival[1]) - float(clock_time))
    else:
        fields['launch_clock_to_boundary_ns'] = None
    external = re.search(r'^\s*(' + number + r')\s+' + number + r'\s+[\^v]\s+input external delay$', before_data, re.M)
    if external:
        delay = float(external[1])
        internal = float(arrival[1]) - delay
        fields.update(input_external_delay_ns=delay, input_to_endpoint_ns=internal,
            input_boundary_required_ns=float(required[1]) - internal)
    if port is not None:
        if not has_slew:
            raise ValueError('boundary report must include slew')
        boundary = re.findall(r'^\s*(' + number + r')\s+' + number + r'\s+(' + number +
            r')\s+([\^v])\s+' + re.escape(port) + r'\s+\((in|out)\)$', before_data, re.M)
        if len(boundary) != 1 or float(boundary[0][0]) < 0:
            raise ValueError('missing, ambiguous or negative boundary slew')
        slew, time, edge, side = boundary[0]
        fields.update(boundary_slew_ns=float(slew), boundary_arrival_ns=float(time),
            boundary_edge='rise' if edge == '^' else 'fall',
            boundary_slew_basis='propagated output' if side == 'out' else 'constrained input')
    if not all(math.isfinite(value) for value in fields.values() if isinstance(value, float)):
        raise ValueError('nonfinite interface timing result')
    return fields


def run(args):
    from recheck_timing import timing_contract

    implementation, work = args.implementation.resolve(), args.work.resolve()
    proof_path = implementation / 'physical_module.json'
    proof = json.loads(proof_path.read_text())
    if proof.get('status') not in ('failed', 'component_checks_pass') or proof.get('top') != 'ADCReceiveLaneController' or not proof.get('native_postroute_exact'):
        raise ValueError('completed routed receive-lane implementation with exact gate replay required')
    for name in ('postroute.v', 'core.spef', 'module_filled.gds'):
        if proof.get('artifact_sha256', {}).get(name) != digest(implementation / name):
            raise ValueError('changed implemented receiver artifact: ' + name)
    hardware = json.loads((implementation / 'hardware_checks.json').read_text())
    hashes = dict(hardware.get('source_sha256', {}))
    if hardware.get('status') != 'pass' or not hashes or any(digest(Path(p)) != value for p, value in hashes.items()):
        raise ValueError('stale mapped receiver source proof')
    work.mkdir(parents=True, exist_ok=True)
    if any(work.iterdir()):
        raise ValueError('interface timing destination must be fresh')
    library = args.pdk.resolve() / 'ihp-sg13g2/libs.ref/sg13g2_stdcell'
    slow, fast = (library / 'lib' / f'sg13g2_stdcell_{suffix}.lib' for suffix in ('slow_1p08V_125C', 'fast_1p32V_m40C'))
    tech, cells = (library / 'lef' / name for name in ('sg13g2_tech.lef', 'sg13g2_stdcell.lef'))
    for path in (slow, fast, tech, cells):
        if proof.get('input_sha256', {}).get(str(path)) != digest(path):
            raise ValueError('changed implementation timing library')
    thresholds = {corner: slew_thresholds(path.read_text()) for corner, path in (('slow', slow), ('fast', fast))}
    netlist, spef = implementation / 'postroute.v', implementation / 'core.spef'
    wrapper = implementation / 'postroute_interface.sv'
    ports = interface_ports(netlist.read_text(), wrapper.read_text())
    if any(any(c in str(path) for c in '{}\n') for path in (slow, fast, tech, cells, netlist, spef, work)):
        raise ValueError('unsupported Tcl path')
    constraints, contract = timing_contract(proof)
    script = work / 'interfaces.tcl'
    script.write_text(f'''read_lef {{{tech}}}
read_lef {{{cells}}}
define_corners slow fast
read_liberty -corner slow {{{slow}}}
read_liberty -corner fast {{{fast}}}
read_verilog {{{netlist}}}
link_design ADCReceiveLaneController
{contract}
set_propagated_clock [all_clocks]
read_spef -corner slow {{{spef}}}
read_spef -corner fast {{{spef}}}
set manifest [open {{{work / 'ports.tsv'}}} w]
set index 0
foreach direction {{to from}} {{
    if {{$direction == "to"}} {{
        set ports [get_ports {{trial_out* acquire_out* convert_out* clear_out evaluate_enable_out}}]
        if {{[llength $ports] != 162}} {{ error "incomplete receive output interface" }}
    }} else {{
        set ports [get_ports comparisons_in*]
        if {{[llength $ports] != 16}} {{ error "incomplete comparator return interface" }}
    }}
    foreach port $ports {{
        foreach delay {{min_rise min_fall max_rise max_fall}} {{
            set file "${{index}}_${{delay}}.rpt"
            puts $manifest "$file\\t$direction\\t[get_full_name $port]"
            report_checks -$direction $port -path_delay $delay -format full_clock_expanded -fields {{slew}} -digits 9 -no_line_splits > "{work}/$file"
        }}
        incr index
    }}
}}
close $manifest
''')
    paths = [SOURCE, SOURCE.with_name('recheck_timing.py'), SOURCE.with_name('physical_module.py'),
        proof_path, implementation / 'hardware_checks.json', script, slow, fast, tech, cells, netlist, spef,
        args.tools.resolve() / 'bin/openroad', wrapper]
    hashes.update({str(path): digest(path) for path in paths})
    with (work / 'openroad.log').open('w') as log:
        execute([str(args.tools.resolve() / 'bin/openroad'), '-exit', str(script)], cwd=work,
            stdout=log, stderr=-2, check=True, timeout=600)
    records = []
    for line in (work / 'ports.tsv').read_text().splitlines():
        file, direction, name = line.split('\t')
        result = read_path((work / file).read_text(), name)
        if name not in ports:
            raise ValueError('unexpected physical interface port: ' + name)
        unused = ports[name]['unused_compatibility']
        if (result is None) != unused:
            raise ValueError('missing active path or unexpected compatibility path: ' + name)
        if result is not None:
            measurement = thresholds[result['corner']]
            edge = result['boundary_edge']
            fraction = (measurement[f'slew_upper_threshold_pct_{edge}'] -
                measurement[f'slew_lower_threshold_pct_{edge}']) / 100
            result['equivalent_full_linear_edge_ns'] = result['boundary_slew_ns'] / fraction
        records.append(dict(port=name, direction=direction, report=file, timing=result, **ports[name]))
    if len(records) != 178 * 4 or any(digest(Path(p)) != value for p, value in hashes.items()):
        raise ValueError('incomplete or changed receive-interface evidence')
    result = dict(status='reported', implementation_status=proof['status'],
        implementation_errors=proof.get('errors', []), constraints=constraints, paths=records,
        liberty_measurement_thresholds=thresholds, source_sha256=hashes,
        scope='worst constrained rising/falling interface paths at both delay polarities; root-clock and local launch origins are distinct; nominal routed RC excludes fill coupling; this report alone does not qualify analog half-cycles')
    (work / 'checks.json').write_text(json.dumps(result, indent=2) + '\n')
    print('Reported receive interfaces:', len(records), 'edge/path views')


if __name__ == '__main__':
    configure_cleanup()
    parser = argparse.ArgumentParser(description=__doc__)
    for name in ('implementation', 'work', 'pdk', 'tools'):
        parser.add_argument('--' + name, type=Path, required=True)
    run(parser.parse_args())
