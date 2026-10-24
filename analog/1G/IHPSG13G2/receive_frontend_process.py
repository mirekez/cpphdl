"""Explicit PSP sizes for the exported receive hierarchy; reject unknown laws."""
from collections import Counter, defaultdict
import math
import re


def selections(filename, parameters):
    """Engineering device choices, not a beta-to-layout parameter conversion."""
    n = lambda beta: (beta, .7, 1)
    p = lambda beta: (beta, .7, -1)
    choices = {
        'nmos_resistor_amplifier.hpp': ([n(.015)] * 2, [50, 50]),
        'nmos_source_follower.hpp': ([n(.1)], [400]),
        'mos_track_hold.hpp': ([n(.08), p(.04), n(.04), p(.02)], [100, 200, 50, 100]),
        'strongarm_comparator.hpp': ([n(.008)] * 3 + [n(.004)] * 2 + [p(.002)] * 6,
                                     [50, 50, 50, 20, 20, 40, 40, 10, 10, 10, 10]),
        'cmos_logic.hpp': ([p(.01), p(.01), n(.01), n(.01)], [60, 60, 30, 30]),
        'nand_latch.hpp': ([n(.05), p(.05)], [120, 240]),
        'level_down.hpp': ([(.0005, .35, -1), n(.004), (.005, .35, -1), (.01, .35, 1)], [2, 5, 20, 10]),
    }
    if filename == 'inverter.hpp':
        for beta, widths in ((.001, [4, 2]), (.004, [20, 10]), (.01, [60, 30]), (.08, [400, 200])):
            expected = [p(beta), n(beta)]
            if match_parameters(parameters, expected):
                return widths
        raise ValueError('unqualified receive inverter parameters')
    if filename not in choices:
        raise ValueError('unqualified receive component: ' + filename)
    expected, widths = choices[filename]
    if not match_parameters(parameters, expected):
        raise ValueError('changed receive MOS inventory/order/parameters: ' + filename)
    return widths


def match_parameters(actual, expected):
    return len(actual) == len(expected) and all(
        all(math.isclose(x, y, rel_tol=1e-12, abs_tol=1e-15) for x, y in zip(a, b))
        for a, b in zip(actual, expected))


def process_deck(model, original, models, count, corner='mos_tt', temperature=27):
    if (count not in (1, 4, 16) or corner not in ('mos_tt', 'mos_ss', 'mos_ff', 'mos_sf', 'mos_fs')
            or not -40 <= temperature <= 125):
        raise ValueError('unsupported receive process experiment')
    marker, suffix = '    analog begin\n', '    end\nendmodule\n'
    if model.count(marker) != 1 or not model.endswith(suffix):
        raise ValueError('changed generated receive contribution block')
    lines = original.splitlines()
    observations = {}
    for line in lines:
        match = re.fullmatch(r'let observation(\d+) = v\((n\d+)\)', line)
        if match:
            observations[int(match[1])] = match[2]
    if (set(observations) != set(range(6 + count)) or lines.count('.control') != 1
            or sum(line.startswith('Ndesign ') for line in lines) != 1
            or sum(line.startswith('Cdevice') for line in lines) != 13 + 59 * count):
        raise ValueError('changed receive host observation/charge interface')
    vdd, core = observations[4], observations[5]
    node = lambda name: '0' if name == 'n0' else name
    annotation = re.compile(r'// device (\d+), IC ([^,]+), .*/([^/]+[.]hpp):\d+')
    resistor = re.compile(r'I\((n\d+), (n\d+)\) <\+ V\((n\d+), (n\d+)\) / ([-+0-9.eE]+);')
    channel = re.compile(r'I\((n\d+), (n\d+)\) <\+ cpphdl_channel\(V\((n\d+), (n\d+)\), '
                         r'V\((n\d+), (n\d+)\), ([-+0-9.eE]+), ([-+0-9.eE]+), ([-+0-9.eE]+)\);')
    owner, filename, identifier = None, None, None
    groups, resistors, devices = defaultdict(list), [], []
    for line in model.split(marker, 1)[1].removesuffix(suffix).splitlines():
        line = line.strip()
        match = annotation.fullmatch(line)
        if match:
            identifier, owner, filename = match.groups()
            continue
        if line.startswith('// device '):
            owner = filename = identifier = None
        if not line or line.startswith('//'):
            continue
        match = resistor.fullmatch(line)
        if match:
            a, b, va, vb, value = match.groups()
            if ((a, b) != (va, vb) or float(value) not in (.25, 10, 50, 150, 250, 500, 1000, 2000, 4000, 10000, 20000, 1e9)):
                raise ValueError('changed receive resistor law')
            resistors.append(f'Rphysical{len(resistors)} {node(a)} {node(b)} {value}')
            continue
        match = channel.fullmatch(line)
        if not match or owner is None:
            raise ValueError('unbound receive law: ' + line)
        a, b, gate, source, drain, drain_source, beta, threshold, polarity = match.groups()
        if (source, drain, drain_source) != (b, a, b):
            raise ValueError('changed receive channel terminal references')
        groups[owner].append(dict(identifier=identifier, filename=filename, drain=a, source=b, gate=gate,
                                  parameters=tuple(map(float, (beta, threshold, polarity)))))
    inventory = []
    components = Counter(parts[0]['filename'] for parts in groups.values())
    expected_components = {'nmos_resistor_amplifier.hpp': 1 + count, 'nmos_source_follower.hpp': 1,
        'mos_track_hold.hpp': count, 'strongarm_comparator.hpp': count, 'cmos_logic.hpp': 2 * count,
        'nand_latch.hpp': count, 'level_down.hpp': count, 'inverter.hpp': 14 * count}
    if components != expected_components:
        raise ValueError('changed receive hierarchy component inventory')
    for owner, parts in groups.items():
        filenames = {part['filename'] for part in parts}
        if len(filenames) != 1:
            raise ValueError('receive instance contains unexpected MOS owners')
        widths = selections(parts[0]['filename'], [part['parameters'] for part in parts])
        for part, width in zip(parts, widths):
            _, threshold, polarity = part['parameters']
            low_voltage = math.isclose(threshold, .35)
            kind = ('lv' if low_voltage else 'hv') + ('_nmos' if polarity > 0 else '_pmos')
            body = '0' if polarity > 0 else node(core if low_voltage else vdd)
            length = .13 if low_voltage else .45
            devices.append(f'Xphysical{part["identifier"]} {node(part["drain"])} {node(part["gate"])} '
                           f'{node(part["source"])} {body} sg13_{kind} w={width}u l={length}u')
            inventory.append(dict(instance=owner, device=part['identifier'], source=part['filename'],
                                  width_um=width, length_um=length, model='sg13_' + kind, body=body))
    if len(devices) != 3 + 59 * count or len(resistors) != 10 + 26 * count:
        raise ValueError('changed complete receive device inventory')
    deck = [lines[0], f'.lib "{models / "cornerMOShv.lib"}" {corner}',
            f'.lib "{models / "cornerMOSlv.lib"}" {corner}', f'.temp {temperature}']
    for line in lines[1:]:
        if line.startswith(('.model CircuitModel ', 'Ndesign ', 'pre_osdi ')):
            continue
        if line == '.control':
            deck += resistors + devices
        deck.append(line)
    result = '\n'.join(deck) + '\n'
    if any(token in result for token in ('CircuitModel', 'design.osdi', 'cpphdl_channel')):
        raise ValueError('generic receive model remains in process deck')
    return result, inventory


def dc_deck(transient):
    head, control = transient.split('.control\n', 1)
    lines = []
    changed = set()
    for line in head.splitlines():
        match = re.fullmatch(r'(Vsource\d+) (\S+) (\S+) PWL\((.+)\)', line)
        if match:
            name, plus, minus, values = match.groups()
            if name in ('Vsource6', 'Vsource7'):
                if minus != '0':
                    raise ValueError('receive stimulus reference changed')
                sign = '.5' if name == 'Vsource6' else '-.5'
                lines.append(f'E{name} {plus} process_center process_difference 0 {sign}')
                changed.add(name)
            else:
                lines.append(f'{name} {plus} {minus} {values.split()[1]}')
        elif not line.startswith('.ic '):
            lines.append(line)
    if changed != {'Vsource6', 'Vsource7'}:
        raise ValueError('receive differential source interface changed')
    observations = [line for line in control.splitlines() if line.startswith('let observation')]
    lines += ['Vprocess_center process_center 0 1.65', 'Vprocess_difference process_difference 0 0',
              '.control', 'set noaskquit', 'set wr_singlescale', 'set wr_vecnames', 'set numdgt=17',
              'dc Vprocess_difference -.24 .24 .12', *observations,
              'wrdata dc.dat observation0 observation1 observation2 observation3 observation4 observation5',
              'quit', '.endc', '.end']
    return '\n'.join(lines) + '\n'
