"""Bind the elaborated repeater to the real padframe's CDL terminals.

This produces a connected implementation netlist. Remaining control/clock
inputs stay explicit until their on-chip producers exist; this is not signoff.
"""
import re


def declarations(source, module):
    match = re.search(r'\bmodule\s+' + re.escape(module) + r'\s*\((.*?)\);', source, re.S)
    if not match:
        raise ValueError('missing module ' + module)
    result = {}
    for text in match[1].split(','):
        found = re.fullmatch(r'\s*(input|output|inout)\s+wire\s*(\[[\d: -]+\])?\s*(\w+)\s*(\[\d+\])?\s*', text)
        if not found or found[3] in result:
            raise ValueError('unsupported or duplicate module terminal: ' + text)
        direction, packed, name, array = found.groups()
        result[name] = dict(direction=direction, packed=packed or '', array=array or '')
    return result


def core_bindings():
    result = dict(VDD='iovdd', VSS='vss', VLOGIC='vdd',
                  ref_clk='ref_clock_core', reset='reset_core',
                  enable_in='enable_core', active_out='active_core')
    for lane in range(8):
        for polarity in ('P', 'N'):
            pad = f'mdi_{"a" if lane < 4 else "b"}_{lane % 4}_{polarity.lower()}'
            result[f'{polarity}_{lane}'] = pad
            result[f'SENSE_{polarity}_{lane}'] = pad + '_core'
    return result


def check(padframe, cdl, electrical, bindings):
    ports = declarations(electrical, 'RepeaterElectrical')
    if bindings != core_bindings():
        raise ValueError('incorrect pad-to-core connection')
    if not set(bindings) <= set(ports):
        raise ValueError('missing core terminal')
    if not padframe.get('common_ground'):
        raise ValueError('analog core requires physically joined VSS/IOVSS')
    header = re.search(r'^\.SUBCKT RepeaterPadframe ([^\n]+)$', cdl, re.M)
    if not header or header[1].split() != padframe['ports']:
        raise ValueError('padframe CDL/interface mismatch')
    if len(padframe['ports']) != len(set(padframe['ports'])):
        raise ValueError('duplicate padframe terminal')
    instances = {}
    for line in cdl.splitlines():
        if line.startswith('X'):
            words = line.split()
            instances[words[0][1:]] = (words[-1], words[1:-1])
    placed = {p['name']: p for p in padframe['placements']}
    if len(placed) != len(padframe['placements']):
        raise ValueError('duplicate pad instance')
    expected_paths = []
    for lane in range(8):
        for polarity in ('p', 'n'):
            name = f'mdi_{"a" if lane < 4 else "b"}_{lane % 4}_{polarity}'
            pad = placed.get(name, {})
            nets = dict(pad=name, padres=name + '_core', vdd='vdd', vss='vss', iovdd='iovdd', iovss='vss')
            if pad.get('cell') != 'sg13g2_IOPadAnalog' or pad.get('nets') != nets:
                raise ValueError('wrong analog pad wiring: ' + name)
            # PDK CDL order: pad, padres, vdd, vss, iovdd, iovss.
            if instances.get(name) != ('sg13g2_IOPadAnalog', [nets[n] for n in
                    ('pad', 'padres', 'vdd', 'vss', 'iovdd', 'iovss')] + ['substrate']):
                raise ValueError('analog pad CDL mismatch: ' + name)
            expected_paths.append(dict(name=name, transmit_pin='pad', transmit_net=name,
                                       receive_pin='padres', receive_net=name + '_core'))
    if sorted(padframe['mdi_paths'], key=lambda p: p['name']) != sorted(expected_paths, key=lambda p: p['name']):
        raise ValueError('incorrect pad transmit/sense paths')
    for name in ('ref_clock', 'reset', 'enable', 'active'):
        output = name == 'active'
        cell = 'sg13g2_IOPadOut4mA' if output else 'sg13g2_IOPadIn'
        nets = dict(pad=name, vdd='vdd', vss='vss', iovdd='iovdd', iovss='vss')
        nets['c2p' if output else 'p2c'] = name + '_core'
        pad = placed.get(name, {})
        if pad.get('cell') != cell or pad.get('nets') != nets:
            raise ValueError('wrong digital pad wiring: ' + name)
        if instances.get(name) != (cell, [name, name + '_core', 'vdd', 'vss', 'iovdd', 'vss', 'substrate']):
            raise ValueError('digital pad CDL mismatch: ' + name)
    if not set(bindings.values()) <= set(padframe['ports']):
        raise ValueError('binding does not reach a padframe terminal')
    for name, p in ports.items():
        if name not in bindings:
            continue
        expected = ('inout' if name in ('VDD', 'VSS', 'VLOGIC') or re.fullmatch(r'[PN]_\d', name)
                    else 'output' if name == 'active_out' else 'input')
        if p['direction'] != expected or p['packed'] or p['array']:
            raise ValueError('incompatible pad/core terminal: ' + name)
    return dict(status='connected_unrouted', full_chip_qualified=False,
                mdi_pads=16, protected_receive_connections=16, direct_transmit_connections=16,
                bindings=bindings,
                unresolved_core_inputs=[name for name, p in ports.items()
                                        if name not in bindings and p['direction'] == 'input'])


def join(padframe, cdl, electrical):
    bindings = core_bindings()
    report = check(padframe, cdl, electrical, bindings)
    ports = declarations(electrical, 'RepeaterElectrical')
    internal = {n for n in padframe['ports'] if n.endswith('_core')}
    external = [n for n in padframe['ports'] if n not in internal]
    if (set(ports) - set(bindings)) & set(padframe['ports']):
        raise ValueError('pad/core name collision')
    def decl(name, p):
        return f"wire{p['packed']} {name}{p['array']}"
    boundary = ['inout wire ' + n for n in external]
    boundary += [p['direction'] + ' ' + decl(name, p) for name, p in ports.items() if name not in bindings]
    lines = ['`default_nettype none', '// Real padframe CDL/GDS implements the I/O blackbox.',
             'module RepeaterPadConnected(\n    ' + ',\n    '.join(boundary) + '\n);']
    lines += ['    wire ' + n + ';' for n in sorted(internal)]
    lines += ['    RepeaterPadframe pads(\n        ' + ',\n        '.join(
        f'.{n}({n})' for n in padframe['ports']) + '\n    );']
    lines += ['    RepeaterElectrical core(\n        ' + ',\n        '.join(
        f'.{n}({bindings.get(n, n)})' for n in ports) + '\n    );', 'endmodule']
    abstract = ['`default_nettype none', '// Physical implementation: RepeaterPadframe CDL and GDS.',
                '(* blackbox *) module RepeaterPadframe(\n    ' + ',\n    '.join(
                    'inout wire ' + n for n in padframe['ports']) + '\n);', 'endmodule']
    return '\n'.join(lines) + '\n', '\n'.join(abstract) + '\n', report
