#!/usr/bin/env python3
"""Screen receiver DC load extremes and Via1–4 currents; not transient or EM signoff."""
import argparse
import json
import math
import os
from pathlib import Path
import re
import sys
import tempfile

SOURCE = Path(__file__).resolve()
ROOT = SOURCE.parents[3]
sys.path[:0] = [str(SOURCE.parent), str(SOURCE.parents[1]),
               str(ROOT / 'VerilogA'), str(ROOT / 'pdk/1G_repeater_IHPSG13G2')]
from receive_upper_current_extracted import evidence
from extract_analog import digest
from input_charge import verify_files
from tool_process import configure_cleanup, execute, memory_slot

# SG13G2 Process Specification revision 1.2, section 2.15, page 16:
# Via1–4: 0.4 mA/cut at 105 C, 11 years. A changed PDK needs review.
SPEC_SHA256 = '974d505886ee62932a50c52c22fbc290db4a70d8a7ce129c0ca8d7934aee160e'
VIA_LIMIT_A = .0004


def pwl_value(encoded, time):
    values = list(map(float, encoded.split()))
    if (len(values) < 4 or len(values) % 2 or not math.isfinite(time) or
            not all(math.isfinite(v) for v in values)):
        raise ValueError('invalid PWL values')
    points = list(zip(values[::2], values[1::2]))
    if any(b[0] <= a[0] for a, b in zip(points, points[1:])):
        raise ValueError('non-increasing PWL times')
    # SPICE holds the first/last value outside the declared breakpoints.
    if time <= points[0][0]:
        return points[0][1]
    if time >= points[-1][0]:
        return points[-1][1]
    for (a, x), (b, y) in zip(points, points[1:]):
        if time <= b:
            return x + (y - x) * (time - a) / (b - a)
    raise ValueError('missing PWL interval')


def dc_deck(original, time):
    if original.count('.control\n') != 1:
        raise ValueError('changed receiver analysis boundary')
    body, control = original.split('.control\n')
    pairs = re.findall(r'^let (observation\d+) = (v\([^\n]+\))$', control, re.M)
    probes = dict(pairs)
    if len(pairs) != 39 or set(probes) != {f'observation{i}' for i in range(39)}:
        raise ValueError('changed receiver observation boundary')
    lines, stimuli, initial_conditions = [], {}, 0
    for line in body.splitlines():
        if line.startswith('.ic '):
            initial_conditions += 1
            continue
        if line.startswith('.save '):
            continue
        if line.startswith(('V', 'I', 'B')):
            match = re.fullmatch(r'(V\S+ \S+ \S+) PWL\((.*)\)', line)
            if match:
                name = match[1].split()[0]
                if name in stimuli:
                    raise ValueError('duplicate independent source')
                value = pwl_value(match[2], time)
                stimuli[name] = value
                line = f'{match[1]} DC {value:.17g}'
            elif re.fullmatch(r'Vsim_timestep_grid cpphdl_sim_timestep_grid 0 PULSE\(0 0 [^\n]+\)', line):
                line = 'Vsim_timestep_grid cpphdl_sim_timestep_grid 0 DC 0'
            else:
                raise ValueError('unhandled independent source: ' + line)
        lines.append(line)
    if len(stimuli) != 17 or not initial_conditions:
        raise ValueError('changed receiver stimulus/initial-state inventory')
    if '.options klu' not in lines:
        lines.append('.options klu')
    lines += ['.control', 'set num_threads=1', 'set noaskquit', 'set filetype=ascii',
              'set numdgt=17', 'op', 'write dc.raw all', 'quit', '.endc', '.end']
    return '\n'.join(lines) + '\n', probes, stimuli


def read_raw(path):
    text = path.read_text()
    count = re.findall(r'^No. Variables:\s*(\d+)$', text, re.M)
    points = re.findall(r'^No. Points:\s*(\d+)$', text, re.M)
    if (len(count) != 1 or points != ['1'] or
            not re.search(r'^Flags:\s+real\s*$', text, re.M) or text.count('Values:\n') != 1):
        raise ValueError('expected one real operating point')
    metadata, encoded = text.split('Values:\n')
    if metadata.count('Variables:\n') != 1:
        raise ValueError('missing DC variable declarations')
    variables = [line.split() for line in metadata.split('Variables:\n')[1].splitlines() if line.strip()]
    values = [line.split() for line in encoded.splitlines() if line.strip()]
    if (len(variables) != int(count[0]) or len(values) != len(variables) or not values or
            any(len(row) != 3 or row[0] != str(i) for i, row in enumerate(variables)) or
            len(values[0]) != 2 or values[0][0] != '0' or any(len(row) != 1 for row in values[1:])):
        raise ValueError('incomplete or reordered DC vectors')
    names = [row[1].lower() for row in variables]
    numbers = [float(values[0][1])] + [float(row[0]) for row in values[1:]]
    if len(set(names)) != len(names) or not all(math.isfinite(v) for v in numbers):
        raise ValueError('duplicate or nonfinite DC vector')
    return dict(zip(names, numbers))


def screen(values, probes):
    observations = {label: values[expr.lower()] for label, expr in probes.items()}
    out, supply, target = [observations[f'observation{i}'] for i in range(3)]
    reference = out - supply * 243 / 443
    divider = target - supply * 243 / 443
    return dict(observations_V=observations, reference_error_V=reference, divider_error_V=divider,
        checks=dict(reference=abs(reference) <= .015, divider=abs(divider) <= .002))


def contact_screen(model, raw, work, geometry):
    import klayout.db as db
    instances = re.findall(r'^Xreference (\S+) (\S+) (\S+) ReceiveReference$', model, re.M)
    if len(instances) != 1 or instances[0][1] != '0':
        raise ValueError('unreviewed physical reference boundary')
    vdd, _, out = instances[0]
    ports = dict(VDD=raw[f'v({vdd})'.lower()], VSS=0., OUT=raw[f'v({out})'.lower()])
    def voltage(node):
        return ports[node] if node in ports else raw[f'v(xreference.{node})'.lower()]
    physical = model.split('.subckt ReceiveReference ', 1)[1].split('.ends', 1)[0]
    edges = []
    for line in physical.splitlines():
        fields = line.split()
        if fields and fields[0].startswith('R'):
            if len(fields) != 4:
                raise ValueError('unexpected extracted resistor')
            resistance = float(fields[3])
            if abs(resistance - 9.0005) < .001:
                edges.append((fields[0], fields[1], fields[2], resistance))
    text = (work / 'extraction/named/ExtractedAnalog.res.ext').read_text()
    scale, = re.findall(r'^scale \S+ \S+ (\S+)$', text, re.M)
    um = float(scale) * .01
    positions = {n: (int(x), int(y)) for n, x, y in re.findall(
        r'^rnode "([^"]+)" \S+ \S+ (-?\d+) (-?\d+) \S+$', text, re.M)}
    layout = db.Layout()
    layout.read(str(work / 'reference.gds'))
    top = layout.top_cell()
    cuts = {layer: db.Region(top.begin_shapes_rec(layout.layer(layer, 0))).merged()
            for layer in (19, 29, 49, 66)}
    contacts, unmapped = [], []
    for name, a, b, resistance in edges:
        current = abs(voltage(a) - voltage(b)) / resistance
        entry = dict(resistor=name, a=a, b=b, resistance_Ohm=resistance, current_A=current,
                     within_limit=current <= VIA_LIMIT_A)
        layers = []
        if a in positions and positions.get(a) == positions.get(b):
            x, y = positions[a]
            point = db.Point(round(x * um / layout.dbu), round(y * um / layout.dbu))
            box = db.Region(db.Box(point.x - 1, point.y - 1, point.x + 1, point.y + 1))
            layers = [layer for layer, region in cuts.items() if region.interacting(box).count()]
            entry['position_um'] = [x * um, y * um]
        entry['possible_via_layers'] = layers
        (contacts if layers else unmapped).append(entry)
    if not contacts:
        raise ValueError('no geometrically identified Via1–4 contacts')
    for group in (contacts, unmapped):
        group.sort(key=lambda item: item['current_A'], reverse=True)
    # Aggregate cut capacity is necessary but cannot establish equal sharing.
    host = model.split('.subckt ReceiveReference ', 1)[0]
    branches = []
    for line in host.splitlines():
        f = line.split()
        if len(f) == 4 and f[0].startswith('R') and out in f[1:3]:
            other = f[2] if f[1] == out else f[1]
            branches.append((raw[f'v({other})'.lower()] - ports['OUT']) / float(f[3]))
    if len(branches) != 16 or sum(branches) <= 0:
        raise ValueError('changed eight-lane external loading boundary')
    sink, = [p for p in geometry['components'] if p['name'] == 'ReceiveCommonMOS10']
    metal = db.Region(top.begin_shapes_rec(layout.layer(30, 0))).merged()
    capacity = {}
    for terminal in ('D', 'S'):
        x, y = [a + b for a, b in zip(sink['origin_um'], sink['geometry']['terminals_um'][terminal])]
        point = db.Point(round(x / layout.dbu), round(y / layout.dbu))
        polygon, = [p for p in metal.each() if p.inside(point)]
        count = cuts[49].interacting(db.Region(polygon)).count()
        capacity[terminal] = dict(cuts=count, maximum_sum_current_A=count * VIA_LIMIT_A,
            external_load_lower_bound_A=sum(branches), necessary_check=count * VIA_LIMIT_A >= sum(branches))
    violations = [c for c in contacts + unmapped if not c['within_limit']]
    return dict(contacts=contacts, unmapped_9ohm_edges=unmapped, violations=violations,
        limit_A=VIA_LIMIT_A, aggregate_via3_capacity=capacity,
        checks=dict(contact_currents=not violations, aggregate_capacity=all(c['necessary_check'] for c in capacity.values())),
        limitations='Nominal high-load DC only. Unmapped edges are screened conservatively without layer attribution. '
                    'Merged contacts, other resistances, wire density, transient currents, PVT and full EM are not qualified.')


def run(args):
    proof, hashes = evidence(args.layout.resolve())
    emitted = json.loads(args.emitted.read_text())
    if emitted.get('status') != 'netlists_emitted' or emitted.get('corner') != 'mos_tt' or emitted.get('temperature_C') != 27:
        raise ValueError('nominal complete receiver deck emission required')
    for path, value in emitted['source_sha256'].items():
        if path in hashes and hashes[path] != value:
            raise ValueError('conflicting emitted/layout provenance')
        hashes[path] = value
    if hashes.get(str(args.layout.resolve())) != digest(args.layout):
        raise ValueError('emission does not reference this layout proof')
    spec = args.pdk.resolve() / 'ihp-sg13g2/libs.doc/doc/SG13G2_os_process_spec.pdf'
    if digest(spec) != SPEC_SHA256:
        raise ValueError('review changed foundry current specification')
    geometry_work = Path(proof['work'])
    positions = geometry_work / 'extraction/named/ExtractedAnalog.res.ext'
    # Coordinate-to-contact attribution must use the recorded extraction,
    # rather than merely hashing whichever coordinate file exists now.
    recorded = proof['extraction']['artifact_sha256'].get(str(positions))
    if recorded is None or recorded != digest(positions):
        raise ValueError('unverified extracted contact coordinates')
    files = [SOURCE, args.emitted.resolve(), args.ngspice.resolve(), spec,
        SOURCE.with_name('receive_upper_current_extracted.py'),
        positions,
        ROOT / '1G/IHPSG13G2/input_charge.py', ROOT / '1G/IHPSG13G2/extract_analog.py',
        ROOT / 'pdk/1G_repeater_IHPSG13G2/tool_process.py']
    hashes.update({str(p): digest(p) for p in files})
    verify_files(hashes)
    args.work.mkdir(parents=True, exist_ok=True)
    work = Path(tempfile.mkdtemp(prefix='run-', dir=args.work.resolve()))
    report = dict(status='running', stage='prepare', work=str(work), scope=__doc__,
        source_sha256=hashes, cases={}, current_limit_basis=dict(spec=str(spec), section='2.15',
        revision='1.2', page=16, lifetime_years=11, temperature_C=105, via1_to_4_A=VIA_LIMIT_A))
    def save():
        text = json.dumps(report, indent=2) + '\n'
        (args.work / 'checks.json').write_text(text)
        (work / 'checks.json').write_text(text)
    models = args.pdk.resolve() / 'ihp-sg13g2/libs.tech/ngspice'
    env = dict(os.environ, PDK_ROOT=str(args.pdk.resolve()), PDK='ihp-sg13g2',
               SPICE_USERINIT_DIR=str(models), OMP_NUM_THREADS='1')
    def operate(directory, text):
        directory.mkdir()
        deck = directory / 'dc.cir'
        deck.write_text(text)
        hashes[str(deck)] = digest(deck)
        with (directory / 'ngspice.log').open('w') as log:
            execute(['prlimit', '--as=1073741824', '--', args.ngspice.resolve(), '-b', deck],
                cwd=directory, env=env, stdout=log, stderr=-2, check=True, timeout=300, memory_exclusive=False)
        log = (directory / 'ngspice.log').read_text()
        if 'ngspice-47 done' not in log or re.search(r'\b(error|fatal|failed|aborted|unknown)\b', log, re.I):
            raise ValueError('incomplete DC analysis')
        return read_raw(directory / 'dc.raw')
    try:
        save()
        with memory_slot(['receive-upper-current-dc'], exclusive=not args.concurrent):
            for label, time in (('low', 16.006e-6), ('high', 16.022e-6)):
                case = report['cases'][label] = dict(time_s=time, flows={})
                for flow in ('schematic', 'physical'):
                    report['stage'] = label + ':' + flow
                    save()
                    source = Path(emitted['work']) / flow / 'measurement.cir'
                    if hashes.get(str(source)) != digest(source):
                        raise ValueError('unverified emitted receiver deck')
                    text, probes, stimuli = dc_deck(source.read_text(), time)
                    directory = work / (label + '-' + flow)
                    values = operate(directory, text)
                    case['flows'][flow] = dict(screen(values, probes), stimuli_V=stimuli)
                    if label == 'high' and flow == 'physical':
                        high_text, high_probes, high_values = text, probes, values
                    save()
                a, b = [case['flows'][f] for f in ('schematic', 'physical')]
                delta = abs(a['observations_V']['observation0'] - b['observations_V']['observation0'])
                case['comparison'] = dict(output_difference_V=delta, limit_V=.005, pass_check=delta <= .005)
                if not all(all(f['checks'].values()) for f in case['flows'].values()) or delta > .005:
                    raise ValueError('DC load screening failed: ' + label)
            report['stage'] = 'unreduced_high_load'
            save()
            physical = (geometry_work / 'extraction/connected.spice').read_text().replace('ExtractedAnalog', 'ReceiveReference')
            text, count = re.subn(r'(?ms)^\.subckt ReceiveReference [^\n]*\n.*?^\.ends[^\n]*\n',
                                 lambda m: physical + '\n', high_text)
            if count != 1:
                raise ValueError('changed physical model boundary')
            # Initial Newton guesses improve convergence; they impose no voltage.
            guesses = ''.join(f'.nodeset {expr}={high_values[expr.lower()]:.17g}\n' for expr in high_probes.values())
            text = text.replace('.control\n', guesses + '.control\n', 1)
            values = operate(work / 'high-unreduced', text)
            departures = {key: abs(values[expr.lower()] - high_values[expr.lower()]) for key, expr in high_probes.items()}
            if max(departures.values()) > 1e-6:
                raise ValueError('unreduced and reduced DC differ beyond 1 uV')
            report['reduction_comparison_V'] = departures
            report['current_screen'] = contact_screen(text, values, geometry_work, proof['geometry'])
            if not all(report['current_screen']['checks'].values()):
                raise ValueError('nominal contact-current screening failed')
        verify_files(hashes)
        report['artifact_sha256'] = {str(p): digest(p) for p in work.rglob('*') if p.suffix in ('.raw', '.log')}
        report.update(status='pass', stage='complete', full_em_qualified=False, transient_qualified=False)
    except BaseException as error:
        report.update(status='fail', error=str(error))
        raise
    finally:
        save()


if __name__ == '__main__':
    configure_cleanup()
    parser = argparse.ArgumentParser(description=__doc__)
    for name in ('layout', 'emitted', 'pdk', 'ngspice', 'work'):
        parser.add_argument('--' + name, type=Path, required=True)
    parser.add_argument('--concurrent', action='store_true', help='use a separately budgeted 1 GiB DC slot')
    run(parser.parse_args())
