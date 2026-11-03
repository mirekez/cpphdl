"""Check buffer-only ECO connectivity in flat OpenROAD Verilog exports.

Only removal of IHP non-inverting A-to-X buffers is accepted. Every surviving
cell, pin, top port and connection must remain identical after contracting the
removed buffers. This proves the Boolean connectivity transformation, not its
delay, signal integrity, placement or routing legality.
"""
import argparse
import hashlib
import json
from pathlib import Path
import re

IDENTIFIER = r'(?:\\[^\s]+|[A-Za-z_$][\w$]*)'
BUFFERS = {f'sg13g2_buf_{drive}' for drive in (1, 2, 4, 8, 16)}
# These characterized delay cells also have X = A in the IHP Liberty models.
# Contracting them proves logic only; their removal must still pass hold timing.
BUFFERS.update(f'sg13g2_dlygate4sd{delay}_1' for delay in (1, 2, 3))
FILLERS = {'sg13g2_decap_8', 'sg13g2_decap_4', 'sg13g2_fill_2', 'sg13g2_fill_1'}


def identifier(value):
    value = value.strip()
    if not re.fullmatch(IDENTIFIER, value):
        raise ValueError('unsupported Verilog identifier: ' + value)
    return value[1:] if value.startswith('\\') else value


def parse(path):
    source = Path(path).read_text()
    source = re.sub(r'/\*.*?\*/|//[^\n]*', '', source, flags=re.S)
    match = re.fullmatch(r'\s*module\s+(' + IDENTIFIER + r')\s*\((.*?)\);(.*)endmodule\s*', source, re.S)
    if not match:
        raise ValueError('expected exactly one flat OpenROAD module')
    module, ports, body = match.groups()
    port_names = [identifier(port) for port in ports.split(',')]
    if len(set(port_names)) != len(port_names):
        raise ValueError('duplicate top port')
    declarations, directions, raw_cells, raw_aliases = {}, {}, {}, []
    for statement in body.split(';'):
        statement = statement.strip()
        if not statement:
            continue
        alias = re.fullmatch(r'assign\s+(.+?)\s*=\s*(.+)', statement)
        if alias:
            raw_aliases.append(alias.groups())
            continue
        declaration = re.fullmatch(r'(input|output|inout|wire)\s+(?:\[(\d+):(\d+)\]\s*)?(' + IDENTIFIER + r')', statement)
        if declaration:
            kind, high, low, name = declaration.groups()
            name = identifier(name)
            bounds = None if high is None else (int(high), int(low))
            if name in declarations and declarations[name] != bounds:
                raise ValueError('conflicting wire widths')
            declarations[name] = bounds
            if kind != 'wire':
                if name in directions:
                    raise ValueError('duplicate port declaration')
                directions[name] = kind
            continue
        instance = re.fullmatch(r'(' + IDENTIFIER + r')\s+(' + IDENTIFIER + r')\s*\((.*)\)', statement, re.S)
        if not instance:
            raise ValueError('unsupported statement in flat connectivity proof: ' + statement[:100])
        master, name, pins = instance.groups()
        name = identifier(name)
        if name in raw_cells:
            raise ValueError('duplicate instance')
        connections = {}
        for pin in (pins.split(',') if pins.strip() else []):
            pin_match = re.fullmatch(r'\s*\.(' + IDENTIFIER + r')\s*\(\s*(.*?)\s*\)\s*', pin, re.S)
            if not pin_match:
                raise ValueError('unsupported pin expression')
            pin_name, net = pin_match.groups()
            pin_name = identifier(pin_name)
            if pin_name in connections:
                raise ValueError('duplicate instance pin')
            connections[pin_name] = net
        raw_cells[name] = (identifier(master), connections)
    if set(port_names) != set(directions):
        raise ValueError('top port declarations differ from module interface')

    def scalar(value):
        if re.fullmatch(r"1'[bB][01]", value):
            return ('$constant', int(value[-1]))
        match = re.fullmatch(r'(' + IDENTIFIER + r')\s*(?:\[(\d+)\])?', value)
        if not match:
            raise ValueError('unsupported scalar connection: ' + value)
        name, bit = match.groups()
        name = identifier(name)
        if name not in declarations:
            raise ValueError('undeclared net: ' + name)
        bounds = declarations[name]
        if bit is None:
            if bounds is not None:
                raise ValueError('whole bus on a scalar cell pin')
            return (name, None)
        bit = int(bit)
        if bounds is None or not min(bounds) <= bit <= max(bounds):
            raise ValueError('out-of-range bit select')
        return (name, bit)

    cells = {name: (master, {pin: scalar(value) for pin, value in pins.items()})
             for name, (master, pins) in raw_cells.items()}
    ports = {}
    for name, direction in directions.items():
        bounds = declarations[name]
        for bit in ([None] if bounds is None else range(min(bounds), max(bounds) + 1)):
            ports[(name, bit)] = direction
    aliases = {}
    for left, right in raw_aliases:
        left, right = scalar(left), scalar(right)
        if ports.get(left) != 'output' or left in aliases:
            raise ValueError('only a single scalar assignment to an output port is supported')
        aliases[left] = right
    interface = [(name, directions[name], declarations[name]) for name in port_names]
    return dict(module=identifier(module), cells=cells, ports=ports, aliases=aliases,
                interface=interface)


def connectivity(design, removed):
    parent = {}

    def root(net):
        parent.setdefault(net, net)
        trail = []
        while parent[net] != net:
            trail.append(net)
            net = parent[net]
        for item in trail:
            parent[item] = net
        return net

    for name in removed:
        master, pins = design['cells'][name]
        if master not in BUFFERS or set(pins) != {'A', 'X'}:
            raise ValueError('ECO removes a cell other than an A-to-X non-inverting buffer: ' + name)
        parent[root(pins['X'])] = root(pins['A'])
    groups = {}

    def add(net, anchor):
        groups.setdefault(root(net), []).append(anchor)

    for net, direction in design['ports'].items():
        add(net, ('port', net[0], str(net[1]), direction))
    for name, (master, pins) in design['cells'].items():
        if name not in removed:
            for pin, net in pins.items():
                add(net, ('cell', name, master, pin))
    # Keep an output alias as a directed identity operator. Do not silently
    # merge its two nets: an ECO must preserve this connection as well.
    for left, right in design.get('aliases', {}).items():
        add(left, ('alias', left[0], str(left[1]), 'X'))
        add(right, ('alias', left[0], str(left[1]), 'A'))
    for bit in (0, 1):
        add(('$constant', bit), ('constant', str(bit)))
    # Nets may be renamed by the tool. Stable physical endpoints identify them.
    return {anchor: min(anchors) for anchors in groups.values() for anchor in anchors}


def compare(before_path, after_path, *, defer_fillers=False):
    before, after = parse(before_path), parse(after_path)
    if before['module'] != after['module'] or before['interface'] != after['interface']:
        raise ValueError('ECO changed the top-level interface')
    old, new = before['cells'], after['cells']
    if set(new) - set(old):
        raise ValueError('buffer-removal ECO inserted cells')
    removed = set(old) - set(new)
    contracted = removed
    if defer_fillers:
        for name in removed:
            if old[name][0] not in FILLERS or old[name][1]:
                raise ValueError('filler deferral removes a functional or connected cell: ' + name)
        # These exports omit physical supply pins. This check only proves
        # signal connectivity; the restored layout still needs full DRC/LVS.
        contracted = set()
    for name in new:
        if new[name][0] != old[name][0] or set(new[name][1]) != set(old[name][1]):
            raise ValueError('ECO changed a surviving cell master or interface: ' + name)
    if connectivity(before, contracted) != connectivity(after, set()):
        raise ValueError('ECO changed non-buffer connectivity')
    return dict(status='pass', top=before['module'], before_cells=len(old), after_cells=len(new),
                removed_buffers=0 if defer_fillers else len(removed),
                removed_fillers=len(removed) if defer_fillers else 0, removed_names=sorted(removed),
                before_sha256=hashlib.sha256(Path(before_path).read_bytes()).hexdigest(),
                after_sha256=hashlib.sha256(Path(after_path).read_bytes()).hexdigest(),
                scope=('unchanged signal endpoints after removing only pinless physical fillers'
                       if defer_fillers else
                       'exact surviving endpoint connectivity after contracting only removed IHP A-to-X buffers'),
                timing_qualified=False, physical_qualified=False)


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    for name in ('before', 'after', 'output'):
        parser.add_argument('--' + name, type=Path, required=True)
    args = parser.parse_args()
    args.output.unlink(missing_ok=True)
    args.output.write_text(json.dumps(compare(args.before, args.after), indent=2) + '\n')
