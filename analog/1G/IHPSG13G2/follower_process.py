"""Bind the exported one-MOS follower to an explicitly sized IHP HV device."""
import math
import re


def process_deck(model, original, models, corner, temperature):
    if corner not in ('mos_tt', 'mos_ss', 'mos_ff', 'mos_sf', 'mos_fs') or not -40 <= temperature <= 125:
        raise ValueError('unsupported process condition')
    marker, suffix = '    analog begin\n', '    end\nendmodule\n'
    if model.count(marker) != 1 or not model.endswith(suffix):
        raise ValueError('changed generated contribution block')
    resistor = re.compile(r'I\((n\d+), (n\d+)\) <\+ V\((n\d+), (n\d+)\) / ([-+0-9.eE]+);')
    channel = re.compile(r'I\((n\d+), (n\d+)\) <\+ cpphdl_channel\(V\((n\d+), (n\d+)\), '
                         r'V\((n\d+), (n\d+)\), ([-+0-9.eE]+), ([-+0-9.eE]+), ([-+0-9.eE]+)\);')
    node = lambda value: '0' if value == 'n0' else value
    devices, passives, channels = [], [], 0
    for line in model.split(marker, 1)[1].removesuffix(suffix).splitlines():
        line = line.strip()
        if not line or line.startswith('//'):
            continue
        match = resistor.fullmatch(line)
        if match:
            a, b, va, vb, value = match.groups()
            if (a, b) != (va, vb) or float(value) not in (10, 50, 500, 10000):
                raise ValueError('unexpected follower passive')
            passives.append(float(value))
            devices.append(f'Rphysical{len(devices)} {node(a)} {node(b)} {value}')
            continue
        match = channel.fullmatch(line)
        if not match:
            raise ValueError('unbound follower law: ' + line)
        a, b, gate, source, drain, drain_source, beta, threshold, sign = match.groups()
        if (source, drain, drain_source) != (b, a, b) or not all(math.isclose(float(x), y, rel_tol=1e-12)
                for x, y in zip((beta, threshold, sign), (.05, .7, 1))):
            raise ValueError('changed follower channel or terminal references')
        # An explicit engineering size, not a beta-to-geometry calculation.
        # The geometry helper folds this total width into valid fingers.
        devices.append(f'Xphysical{len(devices)} {node(a)} {node(gate)} {node(b)} follower_body '
                       'sg13_hv_nmos w=200u l=.45u')
        channels += 1
    lines = original.splitlines()
    if (channels != 1 or sorted(passives) != [10, 50, 500, 10000]
            or sum(line.startswith('Cdevice') for line in lines) != 5 or lines.count('.control') != 1
            or sum(line.startswith('Ndesign ') for line in lines) != 1):
        raise ValueError('changed follower inventory or host interface')
    deck = [lines[0], f'.lib "{models / "cornerMOShv.lib"}" {corner}', f'.temp {temperature}']
    for line in lines[1:]:
        if line.startswith(('.model CircuitModel ', 'Ndesign ', 'pre_osdi ')):
            continue
        if line == '.control':
            deck.extend(devices)
            # Measure substrate current rather than assuming a three-terminal
            # drain/source balance. This ideal 0 V probe preserves body bias.
            deck.append('Vfollower_body follower_body 0 0')
        deck.append(line)
    result = '\n'.join(deck) + '\n'
    if any(token in result for token in ('CircuitModel', 'design.osdi', 'cpphdl_channel')):
        raise ValueError('generic channel remains in process deck')
    return result


def dc_deck(transient):
    head, control = transient.split('.control\n', 1)
    lines, sources = [], []
    for line in head.splitlines():
        match = re.fullmatch(r'(Vsource\d+) (\S+) (\S+) PWL\(.+\)', line)
        if match:
            name, plus, minus = match.groups()
            sources.append(name)
            lines.append(f'{name} {plus} {minus} 2.1')
        elif not line.startswith('.ic '):
            lines.append(line)
    observations = [line for line in control.splitlines() if line.startswith('let observation')]
    if len(sources) != 1 or len(observations) != 4:
        raise ValueError('changed follower source or observation interface')
    lines += ['.control', 'set noaskquit', 'set wr_singlescale', 'set wr_vecnames', 'set numdgt=17',
              f'dc {sources[0]} 2.1 2.9 .2', *observations,
              'wrdata dc.dat observation0 observation1 observation2 i(Vfollower_body)', 'quit', '.endc', '.end']
    return '\n'.join(lines) + '\n'
