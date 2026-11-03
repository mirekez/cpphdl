"""Prove input-pin permutations in an otherwise unchanged flat netlist.

This narrow proof requires stable instance, master and net names. Every changed
cell must have a scalar combinational Liberty-derived model, and its complete
truth table must agree under the actual old/new input connections. It permits
no resizing, buffering, sequential rewiring or physical qualification.
"""
import argparse
import ast
from collections import Counter
import hashlib
import json
from pathlib import Path
import re

from buffer_equivalence import parse
from sizing_equivalence import models


def combinational_model(body):
    header, *statements = body.rstrip(';').split(';')
    if not re.fullmatch(r'\([\w,]+\)', header):
        raise ValueError('unsupported scalar model header')
    declarations = {'input': set(), 'output': set(), 'wire': set()}
    assignments = {}
    for statement in statements:
        declaration = re.fullmatch(r'(input|output|wire)([\w,]+)', statement)
        if declaration:
            kind, names = declaration.groups()
            for name in names.split(','):
                if name in declarations[kind] or not re.fullmatch(r'[A-Za-z_]\w*', name):
                    raise ValueError('duplicate or invalid model declaration')
                declarations[kind].add(name)
            continue
        assignment = re.fullmatch(r'assign([A-Za-z_]\w*)=(.+)', statement)
        if not assignment or assignment[1] in assignments:
            raise ValueError('pin swaps require a scalar combinational model')
        expression = re.sub(r"1'[bB]([01])", r'\1', assignment[2])
        try:
            assignments[assignment[1]] = ast.parse(expression, mode='eval').body
        except SyntaxError as error:
            raise ValueError('unsupported combinational expression') from error
    inputs, outputs, wires = (declarations[k] for k in ('input', 'output', 'wire'))
    if (not inputs or not outputs or len(inputs) > 8 or inputs & outputs or
            inputs & wires or set(header[1:-1].split(',')) != inputs | outputs or
            not outputs <= assignments.keys() or set(assignments) - (outputs | wires)):
        raise ValueError('unsupported scalar combinational interface')
    return inputs, outputs, assignments


def truth_table(model, connections, variables):
    inputs, outputs, assignments = model
    rows = 1 << len(variables)
    mask = (1 << rows) - 1
    values = {pin: sum(((row >> variables.index(connections[pin])) & 1) << row
                       for row in range(rows)) for pin in inputs}
    visiting = set()

    def signal(name):
        if name in values:
            return values[name]
        if name in visiting or name not in assignments:
            raise ValueError('cyclic or undriven combinational model')
        visiting.add(name)
        values[name] = evaluate(assignments[name]) & mask
        visiting.remove(name)
        return values[name]

    def evaluate(node):
        if isinstance(node, ast.Name):
            return signal(node.id)
        if isinstance(node, ast.Constant) and type(node.value) is int and node.value in (0, 1):
            return mask if node.value else 0
        if isinstance(node, ast.UnaryOp) and isinstance(node.op, ast.Invert):
            return (~evaluate(node.operand)) & mask
        if isinstance(node, ast.BinOp):
            left, right = evaluate(node.left), evaluate(node.right)
            if isinstance(node.op, ast.BitAnd):
                return left & right
            if isinstance(node.op, ast.BitOr):
                return left | right
            if isinstance(node.op, ast.BitXor):
                return left ^ right
        raise ValueError('unsupported Boolean operator in cell model')

    return {pin: signal(pin) for pin in outputs}


def compare(before_path, after_path, model_path):
    before, after = parse(before_path), parse(after_path)
    for key in ('module', 'interface', 'ports', 'aliases'):
        if before[key] != after[key]:
            raise ValueError('pin repair changed interface or directed aliases')
    old, new = before['cells'], after['cells']
    if set(old) != set(new):
        raise ValueError('pin repair inserted or removed cells')
    library = models(model_path)
    changed = []
    for name, (master, pins) in old.items():
        target, target_pins = new[name]
        if master != target or master not in library or set(pins) != set(target_pins):
            raise ValueError('pin repair changed a cell master or interface: ' + name)
        if pins == target_pins:
            continue
        model = combinational_model(library[master])
        inputs, outputs, _ = model
        if set(pins) != inputs | outputs:
            raise ValueError('changed cell needs every model port: ' + name)
        if any(pins[p] != target_pins[p] for p in outputs):
            raise ValueError('pin repair changed an output connection: ' + name)
        if Counter(pins[p] for p in inputs) != Counter(target_pins[p] for p in inputs):
            raise ValueError('pin repair changed the input-net multiset: ' + name)
        variables = sorted(set(pins[p] for p in inputs), key=repr)
        if truth_table(model, pins, variables) != truth_table(model, target_pins, variables):
            raise ValueError('pin permutation changes the Boolean function: ' + name)
        changed.append(dict(instance=name, master=master,
                            pins=sorted(p for p in pins if pins[p] != target_pins[p])))
    paths = (before_path, after_path, model_path)
    return dict(status='pass', top=before['module'], changed_cells=changed,
                input_sha256={str(Path(p).resolve()): hashlib.sha256(Path(p).read_bytes()).hexdigest()
                              for p in paths},
                scope='exact combinational truth tables under input permutations; all other connections unchanged',
                timing_qualified=False, physical_qualified=False, full_chip_qualified=False)


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    for name in ('before', 'after', 'models', 'output'):
        parser.add_argument('--' + name, type=Path, required=True)
    args = parser.parse_args()
    args.output.unlink(missing_ok=True)
    result = compare(args.before, args.after, args.models)
    result['source_sha256'] = {str(p.resolve()): hashlib.sha256(p.read_bytes()).hexdigest()
        for p in (Path(__file__), Path(__file__).with_name('buffer_equivalence.py'),
                  Path(__file__).with_name('sizing_equivalence.py'))}
    args.output.write_text(json.dumps(result, indent=2) + '\n')
