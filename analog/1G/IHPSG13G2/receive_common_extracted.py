"""Bind the physical shared reference into the existing eight-lane PSP bench."""
from collections import Counter
import math
from pathlib import Path
import re

from receive_common_layout import PORTS, circuit
from reference_clock_extracted_process import bind_observations
from extract_analog import inventory
from reduce_extracted_wires import reduce


def reference(geometry):
    lines = ['.subckt ReceiveReference ' + ' '.join(PORTS)]
    models = Counter()
    for part in geometry['components']:
        info, nodes, kind = part['geometry'], part['connections'], part['kind']
        if kind == 'mos':
            model, count = info['model'], info['fingers']
            width, length = info['finger_width_um'], info['length_um']
            terminals = [nodes[p] for p in ('D', 'G', 'S', 'B')]
        elif kind == 'resistor':
            model, count = 'rppd', 1
            if info['model'] != model or info.get('bends', 0):
                raise ValueError('unreviewed receive reference resistor')
            width, length = info['width_um'], info['length_um']
            terminals = [nodes[p] for p in ('PLUS', 'MINUS', 'SUB')]
        elif kind == 'capacitor':
            model, count = 'cap_cmim', info.get('capacitors', 1)
            unit = info.get('unit', info)
            width, length = unit['width_um'], unit['length_um']
            if count > 1 and 'columns' not in info:
                raise ValueError('receive reference requires parallel storage banks')
            terminals = [nodes[p] for p in ('PLUS', 'MINUS')]
        else:
            raise ValueError('unknown receive reference physical device')
        if (not isinstance(count, int) or not 1 <= count <= 100 or
                not all(math.isfinite(v) and v > 0 for v in (width, length))):
            raise ValueError('invalid receive reference physical dimensions')
        for unit in range(count):
            lines.append(f'X{part["name"]}_{unit} ' + ' '.join(terminals) +
                f' {model} w={width:g}u l={length:g}u' + (' ic=0' if kind == 'capacitor' else ''))
            models[model] += 1
    if dict(models) != {'sg13_hv_nmos': 210, 'sg13_hv_pmos': 360, 'rppd': 9, 'cap_cmim': 37}:
        raise ValueError('receive reference physical inventory changed: ' + str(models))
    return '\n'.join(lines + ['.ends ReceiveReference', '']), dict(models)


def separate_host(deck, process_inventory, source, probes):
    """Remove exactly the actual IC's laws, keeping all external loads/sources."""
    devices, resistors, capacitors = circuit(source, probes)
    physical = process_inventory['mos']
    if len(devices) != len(physical):
        raise ValueError('receive reference transistor boundary changed')
    observations = dict(re.findall(r'^let observation(\d+) = v\((n\d+)\)$', deck, re.M))
    if set(observations) != set(map(str, range(39))):
        raise ValueError('receive reference host observation interface changed')
    mapping = dict(zip(('OUT', 'VDD', 'TARGET', 'DRIVE', 'BIAS_N', 'BIAS_P', 'TAIL_BIAS'),
                       [observations[str(i)] for i in range(7)])) | {'VSS': '0'}
    removed = []
    for declared, selected in zip(devices, physical):
        if any(declared[k] != selected[k] for k in ('model', 'width_um', 'length_um')):
            raise ValueError('receive layout differs from independently reviewed transistor binding')
        matches = re.findall(r'^Xphysical' + selected['id'] + r' ([^\n]+)$', deck, re.M)
        if len(matches) != 1:
            raise ValueError('missing receive reference transistor')
        fields = matches[0].split()
        if fields[4] != declared['model']:
            raise ValueError('wrong receive reference transistor model')
        for logical, actual in zip((declared[k] for k in ('a', 'gate', 'b', 'body')), fields[:4]):
            if logical in mapping and mapping[logical] != actual:
                raise ValueError('receive reference feedback binding differs')
            mapping[logical] = actual
        removed.append('Xphysical' + selected['id'])
    if len(set(mapping.values())) != len(mapping):
        raise ValueError('receive reference logical nodes are aliased')
    # The lead node is the sole reference node not on a transistor terminal.
    unknown = {n for a, b, _ in resistors + capacitors for n in (a, b)} - mapping.keys()
    if len(unknown) != 1:
        raise ValueError('receive reference compensation boundary changed')
    lead = unknown.pop()
    rs = re.findall(r'^(Rphysical\d+) (\S+) (\S+) (\S+)$', deck, re.M)
    lead_branches = [(a, b) for _, a, b, v in rs if float(v) == 1000 and mapping['DRIVE'] in (a, b)]
    if len(lead_branches) != 1:
        raise ValueError('missing receive reference lead branch')
    mapping[lead] = next(n for n in lead_branches[0] if n != mapping['DRIVE'])
    if len(set(mapping.values())) != len(mapping):
        raise ValueError('receive reference compensation node is aliased')
    cs = re.findall(r'^(Cdevice\d+) (\S+) (\S+) (\S+) IC=(\S+)$', deck, re.M)
    for expected, candidates, kind in ((resistors, rs, 'resistor'), (capacitors, cs, 'capacitor')):
        for a, b, value in expected:
            matches = [p for p in candidates if p[0] not in removed and
                {p[1], p[2]} == {mapping[a], mapping[b]} and
                math.isclose(float(p[3]), value, rel_tol=1e-12)]
            if len(matches) != 1 or (kind == 'capacitor' and float(matches[0][4]) != 0):
                raise ValueError('missing/ambiguous reference ' + kind)
            removed.append(matches[0][0])
    lines = [line for line in deck.splitlines() if not line.split() or line.split()[0] not in removed]
    if len(removed) != 39 or len(deck.splitlines()) - len(lines) != 39:
        raise ValueError('receive reference removal inventory differs')
    # Remaining equations may only meet the IC at its three external ports.
    internal = {v for k, v in mapping.items() if k not in PORTS}
    before_control = '\n'.join(lines).split('.control', 1)[0]
    for line in before_control.splitlines():
        if line.startswith(('R', 'C', 'X', 'V', 'I')) and internal & set(line.split()):
            raise ValueError('receive reference private node is loaded outside its IC')
    return '\n'.join(lines) + '\n', mapping, dict(removed_laws=len(removed), host_mos=0,
        host_resistors=len(rs) - len(resistors), host_capacitors=len(cs) - len(capacitors))


def physical_model(work, geometry, requested, expected):
    work = Path(work) / 'extraction'
    text = (work / 'connected.spice').read_text()
    if dict(inventory(text, PORTS)['models']) != expected | {'rppd': 18}:
        # R3 body-charge adaptation divides each physical resistor into two
        # electrical segments. It does not create a new physical resistor.
        raise ValueError('receive reference extracted device inventory differs')
    raw = (work / 'named/capacitance.spice').read_text()
    nodes = set(PORTS)
    for line in raw.splitlines():
        fields = line.split(';')[0].split()
        if fields and fields[0].startswith(('R', 'C')):
            nodes.update(fields[1:3])
        elif fields and fields[0].startswith('X'):
            index = next(i for i, token in enumerate(fields) if token in expected)
            nodes.update(fields[1:index])
    bindings, audit = bind_observations(geometry, requested,
        (work / 'named/ExtractedAnalog.ext').read_text(), nodes)
    text, reduction = reduce(text, PORTS, list(bindings.values()))
    text = re.sub(r'^(X[^\n]+\bcap_cmim\b[^\n]*)$', r'\1 ic=0', text, flags=re.M)
    text = text.replace('ExtractedAnalog', 'ReceiveReference')
    return text, bindings, dict(bindings=audit, reduction=reduction)


def compose(host, mapping, model, probes):
    if host.count('.control') != 1 or model.count('.subckt ReceiveReference ') != 1:
        raise ValueError('receive reference composition interface changed')
    replacement = {actual: 'Xreference.' + probes[logical]
                   for logical, actual in mapping.items() if logical not in PORTS}
    pattern = re.compile(r'\b(?:' + '|'.join(re.escape(n) for n in replacement) + r')\b')
    host = pattern.sub(lambda match: replacement[match[0]], host)
    instance = 'Xreference ' + ' '.join(mapping[n] for n in PORTS) + ' ReceiveReference\n'
    return host.replace('.control', model + instance + '.control', 1)
