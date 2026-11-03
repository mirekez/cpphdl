"""Resolve the supported IHP low-voltage PSP cards without invoking ngspice.

This is a bounded PDK-card frontend, not an arbitrary SPICE interpreter.
Unknown syntax and parameters fail instead of dropping model behavior.
"""
import ast
import hashlib
import math
from pathlib import Path
import re


def lines(path):
    result = []
    for line in path.read_text().splitlines():
        line = line.strip()
        if not line or line.startswith('*'):
            continue
        if line.startswith('+'):
            if not result:
                raise ValueError('orphan continuation')
            result[-1] += ' ' + line[1:]
        else:
            result.append(line)
    return result


def expression(text, scope):
    source = text.strip().strip("'{}")
    # SPICE suffixes in wrapper defaults; model-card arithmetic stays explicit.
    scales = dict(t=1e12, g=1e9, meg=1e6, k=1e3, m=1e-3, u=1e-6, n=1e-9, p=1e-12, f=1e-15)
    source = re.sub(r'(?<![\w.])(\d+(?:\.\d*)?|\.\d+)(meg|[tgkmunpf])\b',
                    lambda match: f'({match[1]}*{scales[match[2].lower()]})', source, flags=re.I)
    tree = ast.parse(source, mode='eval')

    def evaluate(node):
        if isinstance(node, ast.Expression): return evaluate(node.body)
        if isinstance(node, ast.Constant) and type(node.value) in (int, float): return node.value
        if isinstance(node, ast.Name): return scope[node.id.lower()]
        if isinstance(node, ast.UnaryOp) and isinstance(node.op, (ast.UAdd, ast.USub)):
            return evaluate(node.operand) * (-1 if isinstance(node.op, ast.USub) else 1)
        if isinstance(node, ast.BinOp):
            a, b = evaluate(node.left), evaluate(node.right)
            if isinstance(node.op, ast.Add): return a + b
            if isinstance(node.op, ast.Sub): return a - b
            if isinstance(node.op, ast.Mult): return a * b
            if isinstance(node.op, ast.Div): return a / b
            if isinstance(node.op, ast.Pow): return a ** b
        raise ValueError('unsupported PDK parameter expression: ' + text)
    value = float(evaluate(tree))
    if not math.isfinite(value): raise ValueError('nonfinite PDK parameter')
    return value


def assignments(text, scope):
    pattern = re.compile(r"([A-Za-z_$][\w$]*)\s*=\s*('[^']*'|\{[^}]*\}|[^\s]+)")
    end, result = 0, {}
    for match in pattern.finditer(text):
        if text[end:match.start()].strip(): raise ValueError('unparsed PDK parameter text')
        name = match[1].lower()
        if name in result: raise ValueError('duplicate PDK parameter: ' + name)
        result[name] = expression(match[2], scope)
        end = match.end()
    if text[end:].strip(): raise ValueError('unparsed PDK parameter tail')
    return result


def psp(pdk, corner='mos_tt', polarity='n', width=1e-6, length=.13e-6, voltage='lv'):
    if corner not in ('mos_tt', 'mos_ss', 'mos_ff') or polarity not in ('n', 'p'):
        raise ValueError('only deterministic TT/SS/FF low-voltage MOS cards are supported')
    if voltage not in ('lv', 'hv'): raise ValueError('unsupported voltage family')
    minimum_width = .15e-6 if voltage == 'lv' else .30e-6
    minimum_length = .13e-6 if voltage == 'lv' else (.45e-6 if polarity == 'n' else .40e-6)
    if not minimum_width <= width <= 10e-6 or not minimum_length <= length <= 10e-6:
        raise ValueError('geometry outside the documented model range')
    directory = Path(pdk).resolve() / 'ihp-sg13g2/libs.tech/ngspice/models'
    library = directory / f'cornerMOS{voltage}.lib'
    wrapper = directory / f'sg13g2_mos{voltage}_mod.lib'
    card = directory / f'sg13g2_mos{voltage}_parm.lib'
    scope = dict(w=width, l=length, ng=1, m=1, mm_ok=0, pre_layout=1, rfmode=0)
    wrappers = [line for line in lines(wrapper) if line.lower().startswith(f'.subckt sg13_{voltage}_{polarity}mos ')]
    if len(wrappers) != 1: raise ValueError('missing or ambiguous device wrapper')
    fields = wrappers[0].split(None, 6)
    if fields[2:6] != ['d', 'g', 's', 'b']: raise ValueError('unsupported wrapper terminal order')
    defaults = assignments(fields[6], {})
    expected = dict(ng=1, m=1, mm_ok=0, **{'as': 0}, ad=0, pd=0, ps=0, trise=0,
                    z1=.34e-6, z2=.38e-6, wmin=.15e-6, rfmode=0, pre_layout=1)
    if {key: value for key, value in defaults.items() if key not in ('w', 'l')} != expected:
        raise ValueError('PDK wrapper defaults changed; review supported schematic lowering')
    scope.update(defaults)
    scope.update(w=width, l=length)
    # Include the global parameter declarations used by the selected wrapper.
    for line in lines(wrapper):
        if line.lower().startswith('.subckt '): break
        if line.lower().startswith('.param '): scope.update(assignments(line[7:], scope))
    active, found = False, False
    for line in lines(library):
        words = line.split()
        if words[0].lower() == '.lib':
            active = words[1].lower() == corner
            found |= active
        elif words[0].lower() == '.endl': active = False
        elif active:
            if words[0].lower() == '.param': scope.update(assignments(line[7:], scope))
            elif voltage == 'hv' and line.lower() == '.include sg13g2_svaricaphv_mod.lib':
                # This declares a separate, uninstantiated varactor subcircuit;
                # its wrapper is not part of a MOS transistor instance.
                included = lines(directory / words[1])
                if not included[0].lower().startswith('.subckt ') or not included[-1].lower().startswith('.ends'):
                    raise ValueError('unexpected global content in varactor wrapper')
            elif line.lower() != f'.include sg13g2_mos{voltage}_mod.lib':
                raise ValueError('unsupported corner statement: ' + line)
    if not found: raise ValueError('missing corner')
    name = f'sg13g2_{voltage}_{polarity}mos_psp'
    selected = [line for line in lines(card) if line.lower().startswith('.model ' + name + ' ')]
    if len(selected) != 1: raise ValueError('missing or ambiguous PSP card')
    _, _, module, text = selected[0].split(None, 3)
    model = assignments(text, scope)
    # SPICE's level selects a model implementation; OSDI selects it by module.
    # Assert the expected level rather than forwarding an unknown device field.
    if model.pop('level') != 103.60 or module.lower() != 'psp103va':
        raise ValueError('unsupported compact-model revision')
    # Match the wrapper's default ng=1, m=1, rfmode=0, schematic geometry.
    instance = dict(w=width, l=length, nf=1, mult=1, **{'as': width*.34e-6},
                    ad=width*.34e-6, ps=2*(width+.34e-6), pd=2*(width+.34e-6),
                    dta=0, ngcon=2, delvto=0, factuo=1)
    binary = directory.parent / 'osdi/psp103.osdi'
    hashes = {str(p): hashlib.sha256(p.read_bytes()).hexdigest() for p in (library, wrapper, card, binary)}
    return dict(module=module, model=model, instance=instance, binary=str(binary),
                corner=corner, polarity=polarity, width=width, length=length, voltage=voltage, input_sha256=hashes)


def write(bundle, destination):
    text = ['module ' + bundle['module']]
    for kind in ('model', 'instance'):
        text += [f'{kind} {key} {value:.17g}' for key, value in sorted(bundle[kind].items())]
    Path(destination).write_text('\n'.join(text) + '\n')
