#!/usr/bin/env python3
"""Partition exported IC contributions into small OSDI models on shared SPICE wires."""
import argparse
from collections import OrderedDict
import hashlib
import json
from pathlib import Path
import re

IDENT = r'[A-Za-z_][A-Za-z_0-9]*'
NODE_CALL = re.compile(r'\b(?:I|V)\(\s*(' + IDENT + r'|0)\s*(?:,\s*(' + IDENT + r'|0)\s*)?\)')


def digest(text):
    return hashlib.sha256(text.encode()).hexdigest()


def rename(text, replacements):
    return re.sub(r'\b' + IDENT + r'\b', lambda match: replacements.get(match[0], match[0]), text)


def wrapped(tokens):
    lines, current = [], ''
    for token in tokens:
        if current and len(current) + len(token) + 1 > 100:
            lines.append(current)
            current = '+ ' + token
        else:
            current += (' ' if current else '') + token
    return '\n'.join([*lines, current])


def partition(model, directory):
    """Preserve every contribution and node; grouping changes compiler units only."""
    directory = Path(directory).resolve()
    directory.mkdir(parents=True, exist_ok=True)
    declaration = re.search(r'\bmodule\s+(' + IDENT + r')\s*\(([^;]*)\)\s*;', model)
    if declaration is None or len(re.findall(r'\bmodule\s+', model)) != 1:
        raise ValueError('partition requires one exported Verilog-A module')
    name = declaration[1]
    ports = [port.strip() for port in declaration[2].split(',')]
    if not ports or any(re.fullmatch(IDENT, port) is None for port in ports) or len(set(ports)) != len(ports):
        raise ValueError('invalid exported port list')
    marker, ending = '    analog begin\n', '    end\nendmodule\n'
    if model.count(marker) != 1 or not model.endswith(ending):
        raise ValueError('partition supports the exported continuous analog block')
    prefix, body = model.split(marker)
    body = body.removesuffix(ending)
    functions = re.findall(r'\s*analog function\b.*?\bendfunction\s*', prefix, re.S)
    rest = prefix
    for function in functions:
        rest = rest.replace(function, '\n', 1)
    rest = re.sub(r'//[^\n]*|`include\s+"disciplines[.]vams"', '', rest)
    rest = re.sub(r'\bmodule\s+' + IDENT + r'\s*\([^;]*\)\s*;', '', rest)
    rest = re.sub(r'\b(?:inout|electrical)\s+[^;]+;', '', rest)
    if rest.strip():
        raise ValueError('unsupported module declarations in partition input')
    declared = set()
    for line in re.findall(r'\belectrical\s+([^;]+);', prefix):
        declared.update(value.strip() for value in line.split(','))
    if not set(ports) <= declared:
        raise ValueError('exported electrical ports are incomplete')
    folded = [node.lower() for node in declared]
    if len(set(folded)) != len(folded):
        raise ValueError('SPICE cannot preserve nodes that differ only by letter case')
    groups = OrderedDict()
    owner, annotation = None, None
    identifiers = set()
    total = 0
    pending_driver = None
    for line in body.splitlines():
        line = line.strip()
        match = re.fullmatch(r'// device (\d+), (.*)', line)
        if match:
            if pending_driver is not None:
                raise ValueError('incomplete paired simulation driver')
            if match[1] in identifiers:
                raise ValueError('duplicate exported device identity')
            identifiers.add(match[1])
            annotation = line
            instance = re.match(r'IC ([^,]+), ', match[2])
            owner = instance[1] if instance else 'host'
            continue
        if not line or line.startswith('//'):
            continue
        contribution = line.split('//', 1)[0].strip()
        if (annotation is None or re.fullmatch(r'I\(' + IDENT + r', ' + IDENT + r'\) <\+ .+;', contribution) is None):
            raise ValueError('partition requires one identified current contribution per line')
        if pending_driver is not None:
            direct = re.fullmatch(r'I\((' + IDENT + r'), (' + IDENT + r')\) <\+ V\((' + IDENT + r')\) '
                                  r'\* V\((' + IDENT + r'), (' + IDENT + r')\) / ([-+0-9.eE]+);', contribution)
            if (not line.endswith('// sim_driver') or direct is None
                    or (direct[1], direct[3], direct[6]) != pending_driver
                    or (direct[1], direct[2]) != (direct[4], direct[5])):
                raise ValueError('changed paired simulation driver')
            pending_driver = None
        elif line.endswith('// sim_driver'):
            inverse = re.fullmatch(r'I\((' + IDENT + r'), (' + IDENT + r')\) <\+ \(1.0 - V\((' + IDENT + r')\)\) '
                                   r'\* V\((' + IDENT + r'), (' + IDENT + r')\) / ([-+0-9.eE]+);', contribution)
            if inverse is None or (inverse[1], inverse[2]) != (inverse[4], inverse[5]):
                raise ValueError('changed paired simulation driver')
            pending_driver = (inverse[1], inverse[3], inverse[6])
        if re.search(r'\bI\s*\(', contribution.split('<+', 1)[1]):
            raise ValueError('cross-instance branch-current probes cannot be partitioned')
        used = []
        for call in NODE_CALL.finditer(contribution):
            for node in call.groups():
                if node is not None and node != '0' and node not in used:
                    if node not in declared:
                        raise ValueError('contribution refers to undeclared node ' + node)
                    used.append(node)
        if not used:
            raise ValueError('contribution has no electrical terminals')
        group = groups.setdefault(owner, [])
        group.append(dict(law=contribution, nodes=used, source=annotation))
        total += 1
        if pending_driver is None:
            annotation = None
    if pending_driver is not None:
        raise ValueError('incomplete paired simulation driver')
    if total == 0:
        raise ValueError('empty contribution graph')
    helper_text = '\n'.join(function.strip() for function in functions)
    if NODE_CALL.search(helper_text) or re.search(r'\b(?:ddt|idt|absdelay|white_noise|flicker_noise)\s*\(', helper_text):
        raise ValueError('partition helpers must depend only on their scalar arguments')
    masters, instances, cards = {}, [], []
    for index, (owner, parts) in enumerate(groups.items()):
        nodes = list(dict.fromkeys(node for part in parts for node in part['nodes']))
        local = ['p' + str(i) for i in range(len(nodes))]
        normalized = [rename(part['law'], dict(zip(nodes, local))) for part in parts]
        signature = digest(helper_text + '\n' + '\n'.join(normalized))
        master = 'AnalogLeaf_' + signature[:16]
        # Prove the canonicalized instance expands to the original laws.
        if [rename(law, dict(zip(local, nodes))) for law in normalized] != [part['law'] for part in parts]:
            raise ValueError('partition changed a contribution or node alias')
        text = ('// Generated IC contributions; source instances and node bindings are in the hierarchy metadata.\n'
                '`include "disciplines.vams"\n\nmodule ' + master + '(' + ', '.join(local) + ');\n'
                '    inout ' + ', '.join(local) + ';\n    electrical ' + ', '.join(local) + ';\n\n' + helper_text +
                '\n\n    analog begin\n' + '\n'.join('        ' + law for law in normalized) + '\n    end\nendmodule\n')
        if master in masters and masters[master]['sha256'] != digest(text):
            raise ValueError('partition model identity collision')
        masters[master] = dict(file=master + '.va', sha256=digest(text), contribution_count=len(parts), ports=local)
        (directory / (master + '.va')).write_text(text)
        instances.append(dict(name='Nleaf' + str(index), owner=owner, master=master, nodes=nodes,
            contributions=[part['source'] for part in parts]))
        cards.append(wrapped(['Nleaf' + str(index), *nodes, master]))
    circuit = ('* Shared-node IC contribution hierarchy generated by C++HDL.\n' +
               '\n'.join('.model ' + master + ' ' + master for master in masters) + '\n' +
               wrapped(['.subckt', name, *ports]) + '\n' + '\n'.join(cards) + '\n.ends ' + name + '\n')
    circuit_file = name + '.cir'
    metadata_file = name + '.hierarchy.json'
    (directory / circuit_file).write_text(circuit)
    result = dict(module=name, ports=ports, contribution_count=total, model_sha256=digest(model),
        partitioner_sha256=digest(Path(__file__).read_text()),
        masters=masters, instances=instances, circuit_sha256=digest(circuit),
        circuit_file=circuit_file, metadata_file=metadata_file,
        scope='same current equations on the same wires in one global SPICE solve; no process-model substitution')
    (directory / metadata_file).write_text(json.dumps(result, indent=2) + '\n')
    return result


def experiment(deck, result, directory):
    """Replace only the original OSDI instance/model/load, preserving the bench."""
    directory = Path(directory).resolve()
    if any(character in str(directory) for character in ('"', '\n', '\r')):
        raise ValueError('unsupported SPICE output path')
    models = re.findall(r'^\.model\s+(\S+)\s+' + re.escape(result['module']) + r'\s*$', deck, re.M)
    if len(models) != 1:
        raise ValueError('experiment must contain one matching exported OSDI model')
    model_name = models[0]
    instances = re.findall(r'^(N\S+)\s+([^\n]+?)\s+' + re.escape(model_name) + r'\s*$', deck, re.M)
    if len(instances) != 1 or len(instances[0][1].split()) != len(result['ports']):
        raise ValueError('experiment instance does not match exported electrical ports')
    if deck.splitlines().count('.control') != 1 or len(re.findall(r'^pre_osdi\s+.+$', deck, re.M)) != 1:
        raise ValueError('experiment must contain one model load and analysis block')
    rewritten = re.sub(r'^\.model\s+' + re.escape(model_name) + r'\s+' + re.escape(result['module']) + r'\s*$',
                       lambda _: '.include "' + str(directory / result['circuit_file']) + '"', deck, flags=re.M)
    instance_name, connections = instances[0]
    rewritten = re.sub(r'^' + re.escape(instance_name) + r'\s+[^\n]+?\s+' + re.escape(model_name) + r'\s*$',
                       lambda _: wrapped(['Xhierarchy', *connections.split(), result['module']]), rewritten, flags=re.M)
    # pre_osdi keeps quote characters in its filename argument in ngspice.
    # Generated master filenames need no quoting and resolve beside the deck.
    loads = '\n'.join('pre_osdi ' + master + '.osdi' for master in result['masters'])
    rewritten = re.sub(r'^pre_osdi\s+.+$', lambda _: loads, rewritten, flags=re.M)
    (directory / 'bench.cir').write_text(rewritten)
    result['experiment_sha256'] = digest(deck)
    result['partitioned_experiment_sha256'] = digest(rewritten)
    (directory / result['metadata_file']).write_text(json.dumps(result, indent=2) + '\n')
    return rewritten


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--partition', type=Path, required=True, help='exported IC or experiment Verilog-A file')
    parser.add_argument('--output', type=Path, required=True, help='directory for leaf models, SPICE library and bindings')
    parser.add_argument('--experiment', type=Path, help='optional exported experiment deck to preserve')
    args = parser.parse_args()
    result = partition(args.partition.read_text(), args.output)
    if args.experiment:
        experiment(args.experiment.read_text(), result, args.output)
    print('Partitioned', result['contribution_count'], 'contributions into', len(result['instances']),
          'IC instances and', len(result['masters']), 'distinct models')


if __name__ == '__main__':
    main()
