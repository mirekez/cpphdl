"""Compose the actual TX connectivity from separately extracted component models.

The assembly preserves each macro's RC network. Inter-macro connections are
ideal until a complete routed transmitter is extracted; this intermediate
model is for detecting loading and startup failures before that layout step.
"""
import json
from pathlib import Path
import re

from transmit_physical_partition import validate_partition
from transmit_storage_layout import storage_partition


INTERFACES = {
    'translator': ['VDD', 'VSS', 'VLOGIC', 'BIAS', 'A', 'Y', 'Y_N'],
    'translation_bias': ['VDD', 'VSS', 'VLOGIC', 'BIAS'],
    'current_bias': ['VDD', 'VSS', 'BIAS', 'CASCODE', *['TRIM_N' + str(i) for i in range(6)]],
    'main': ['P', 'N', 'GP', 'GN', 'BIAS', 'CASCODE', 'VSS', 'TAIL', 'LOWER'],
    'history': ['P', 'N', 'GP', 'GN', 'BIAS', 'CASCODE', 'VSS', 'TAIL', 'LOWER'],
    'main_storage': ['P', 'N', 'GP', 'GN', 'BIAS', 'CASCODE', 'VSS', 'TAIL', 'LOWER'],
    'history_storage': ['P', 'N', 'GP', 'GN', 'BIAS', 'CASCODE', 'VSS', 'TAIL', 'LOWER'],
    'output_storage': ['VDD', 'VSS', 'P', 'N'],
    'bias_storage': ['G', 'SUB'],
    'cascode_storage': ['G', 'SUB'],
    'pullup': ['PLUS', 'MINUS', 'SUB'],
}


def namespace_model(text, original, prefix, ports):
    """Rename local definitions only; retain every device and RC equation."""
    if not re.fullmatch(r'[A-Za-z][A-Za-z0-9_]*', prefix):
        raise ValueError('invalid transmitter model namespace')
    # Normalize only SPICE continuations before inspecting the interface.
    joined = re.sub(r'\n[ \t]*\+[ \t]*', ' ', text)
    definitions = re.findall(r'^\s*\.subckt\s+(\S+)\s+([^\n]+)', joined, re.M | re.I)
    roots = [(n, p.split()) for n, p in definitions if n.lower() == original.lower()]
    if len(roots) != 1 or roots[0][1] != ports:
        raise ValueError('transmitter component model port order differs: ' + prefix)
    names = [n for n, _ in definitions]
    names += re.findall(r'^\s*\.model\s+(\S+)', joined, re.M | re.I)
    if len(names) != len({n.lower() for n in names}):
        raise ValueError('duplicate local transmitter model definition')
    if any(not re.fullmatch(r'[A-Za-z][A-Za-z0-9_]*', n) for n in names):
        raise ValueError('unsupported local transmitter model name')
    rename = {n.lower(): prefix + '_' + n for n in names}
    result = re.sub(r'\b(?:' + '|'.join(re.escape(n) for n in names) + r')\b',
                    lambda m: rename[m[0].lower()], text, flags=re.I)
    return result.rstrip('\n') + '\n', rename[original.lower()]


def assemble(directory, models, name='ConnectedTransmitter'):
    """Each model supplies (SPICE text, root name, actual ordered port list).

    Callers must validate each model's geometry and artifact provenance. This
    function checks the exported topology, complete retained-passive ownership,
    and each instance interface. It does not certify the supplied physics.
    """
    directory = Path(directory)
    validate_partition(directory)
    storage = storage_partition(directory)
    manifest = json.loads((directory / 'partition.json').read_text())
    if set(models) != set(INTERFACES) or not re.fullmatch(r'[A-Za-z][A-Za-z0-9_]*', name):
        raise ValueError('incomplete or ambiguous transmitter component models')
    definitions, roots, model_ports = [], {}, {}
    for kind, interface in INTERFACES.items():
        text, original, ports = models[kind]
        if len(ports) != len(interface) or set(ports) != set(interface):
            raise ValueError('changed transmitter component interface: ' + kind)
        definition, roots[kind] = namespace_model(text, original, name + '_' + kind, ports)
        definitions.append(definition)
        model_ports[kind] = ports
    external = manifest['ports']
    aliases = {node: port for port, node in external.items()}
    net = lambda node: aliases.get(node, 'node_' + str(node))
    blocks = {block['name']: block for block in manifest['blocks']}
    instances, records = [], []

    def instance(instance_name, kind, bindings, elements):
        ports = model_ports[kind]
        if set(bindings) != set(ports):
            raise ValueError('missing transmitter instance connection: ' + instance_name)
        nodes = [net(bindings[p]) for p in ports]
        instances.append('X' + instance_name + ' ' + ' '.join(nodes) + ' ' + roots[kind])
        records.append(dict(name=instance_name, kind=kind, ports=dict(zip(ports, nodes)), elements=elements))

    for index in range(22):
        block = blocks[f'translator_{index}']
        instance(block['name'], 'translator', block['ports'], block['elements'])
    for kind in ('translation_bias', 'current_bias'):
        block = blocks[kind]
        instance(kind, kind, block['ports'], block['elements'])
    for bank in ('main', 'history'):
        for index in range(4):
            block = blocks[f'{bank}_{index}']
            instance(block['name'], bank, block['ports'], block['elements'])
            group = storage['groups'][block['name']]
            instance(block['name'] + '_storage', bank + '_storage', group['ports'],
                     [c['element'] for c in group['capacitors']])
    shared = storage['groups']['shared']
    instance('output_storage', 'output_storage', {p: shared['ports'][p] for p in INTERFACES['output_storage']},
             [c['element'] for c in shared['capacitors'][2:]])
    for kind, capacitor in zip(('bias_storage', 'cascode_storage'), shared['capacitors'][:2]):
        a, b = capacitor['nodes']
        instance(kind, kind, dict(G=a, SUB=b), [capacitor['element']])
    for index, pullup in enumerate(storage['pullups']):
        a, b = pullup['nodes']
        instance('pullup_' + str(index), 'pullup', dict(PLUS=a, MINUS=b, SUB=external['VSS']), [pullup['element']])
    claimed = [element for record in records for element in record['elements']]
    if len(records) != 45 or sorted(claimed) != list(range(manifest['elements'])):
        raise ValueError('transmitter physical assembly omits or duplicates contributions')
    text = ''.join(definitions) + '.subckt ' + name + ' ' + ' '.join(external) + '\n'
    text += '\n'.join(instances) + '\n.ends ' + name + '\n'
    return text, dict(instances=records, original_contributions=len(claimed), ports=list(external),
        scope='connected component models with ideal inter-macro wires; requires independent loaded electrical checks and full routed extraction')
