"""Bind the live SAR experiment to PSP, preserving explicit host-only controls."""
from collections import Counter, defaultdict
import math
import re

from receive_frontend_process import selections


def process_deck(model, original, models, count, metadata, corner='mos_tt', temperature=27, input_drivers=1):
    if (type(input_drivers) is not int or input_drivers not in (1, 2) or count not in (1, 4, 16) or corner not in ('mos_tt', 'mos_ss', 'mos_ff', 'mos_sf', 'mos_fs')
            or not -40 <= temperature <= 125):
        raise ValueError('unsupported closed receive process experiment')
    marker, suffix = '    analog begin\n', '    end\nendmodule\n'
    if model.count(marker) != 1 or not model.endswith(suffix):
        raise ValueError('changed live receive contribution block')
    lines = original.splitlines()
    observations = dict((int(i), n) for i, n in re.findall(r'^let observation(\d+) = v\((n\d+)\)$', original, re.M))
    if (set(observations) != set(range(6 + 2 * count)) or lines.count('.control') != 1
            or sum(line.startswith('Ndesign ') for line in lines) != 1
            or sum(line.startswith('Cdevice') for line in lines) != 13 + 60 * count + 10 * (input_drivers - 1)):
        raise ValueError('changed live receive observation/charge interface')
    vdd, core = observations[4], observations[5]
    expected_controls = {(i, bit) for i in range(count) for bit in range(9)}
    controls = metadata.get('drivers', [])
    if (len(controls) != 9 * count or {(d['slice'], d['bit']) for d in controls} != expected_controls
            or len({d['id'] for d in controls}) != 9 * count
            or metadata.get('decisions') != [observations[7 + 2 * i] for i in range(count)]):
        raise ValueError('changed live receive feedback/control interface')
    node = lambda name: '0' if name == 'n0' else name
    annotation = re.compile(r'// device (\d+), IC ([^,]+), .*/([^/]+[.]hpp):\d+')
    resistor = re.compile(r'I\((n\d+), (n\d+)\) <\+ V\((n\d+), (n\d+)\) / ([-+0-9.eE]+);')
    channel = re.compile(r'I\((n\d+), (n\d+)\) <\+ cpphdl_channel\(V\((n\d+), (n\d+)\), '
                         r'V\((n\d+), (n\d+)\), ([-+0-9.eE]+), ([-+0-9.eE]+), ([-+0-9.eE]+)\);')
    host = re.compile(r'I\((n\d+), (n\d+)\) <\+ (\(1.0 - V\(cpphdl_bit(\d+)\)\)|V\(cpphdl_bit(\d+)\)) '
                      r'\* V\((n\d+), (n\d+)\) / 50.0; // sim_driver')
    owner = filename = identifier = None
    groups, resistors, drivers = defaultdict(list), [], defaultdict(list)
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
                raise ValueError('changed live receive resistor law')
            resistors.append(f'Rphysical{len(resistors)} {node(a)} {node(b)} {value}')
            continue
        match = host.fullmatch(line)
        if match:
            a, b, expression, inverse_id, direct_id, va, vb = match.groups()
            if owner is not None or (a, b) != (va, vb):
                raise ValueError('changed host control terminal references')
            drivers[int(inverse_id or direct_id)].append((a, b, inverse_id is not None))
            continue
        match = channel.fullmatch(line)
        if not match or owner is None:
            raise ValueError('unbound live receive law: ' + line)
        a, b, gate, source, drain, drain_source, beta, threshold, polarity = match.groups()
        if (source, drain, drain_source) != (b, a, b):
            raise ValueError('changed live receive channel references')
        groups[owner].append(dict(identifier=identifier, filename=filename, drain=a, source=b, gate=gate,
                                  parameters=tuple(map(float, (beta, threshold, polarity)))))
    expected_components = {'nmos_resistor_amplifier.hpp': input_drivers + count, 'nmos_source_follower.hpp': input_drivers,
        'mos_track_hold.hpp': count, 'strongarm_comparator.hpp': count, 'cmos_logic.hpp': 2 * count,
        'nand_latch.hpp': count, 'level_down.hpp': count, 'inverter.hpp': 14 * count}
    if Counter(parts[0]['filename'] for parts in groups.values()) != expected_components:
        raise ValueError('changed live receive component inventory')
    if input_drivers == 2:
        amplifiers = [parts for parts in groups.values() if parts[0]['filename'] == 'nmos_resistor_amplifier.hpp'
            and [part['gate'] for part in parts] == [observations[0], observations[1]]]
        followers = [parts for parts in groups.values() if parts[0]['filename'] == 'nmos_source_follower.hpp']
        if (len(amplifiers) != 2 or len(followers) != 2
                or any([part['drain'] for part in parts] != [part['drain'] for part in amplifiers[0]] for parts in amplifiers)
                or amplifiers[0][1]['drain'] != observations[2]
                or any(len(parts) != 1 or (parts[0]['drain'], parts[0]['gate'], parts[0]['source']) !=
                    (vdd, observations[2], observations[3]) for parts in followers)):
            raise ValueError('parallel input drive paths do not share the intended physical nodes')
    devices, inventory = [], []
    for owner, parts in groups.items():
        if len({part['filename'] for part in parts}) != 1:
            raise ValueError('unexpected MOS owners in live receive instance')
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
    if (len(devices) != 3 * input_drivers + 59 * count or len(resistors) != 11 + 26 * count + 6 * (input_drivers - 1)
            or set(drivers) != {d['id'] for d in controls}):
        raise ValueError('changed live receive device/control inventory')
    host_devices = []
    for control, branches in drivers.items():
        if (len(branches) != 2 or branches[0][0] != branches[1][0]
                or branches[0][1:] != ('n0', True) or branches[1][1:] != (vdd, False)):
            raise ValueError('changed finite-impedance host driver')
        output = node(branches[0][0])
        # This adapter is part of the testbench. Its current returns to the
        # solved supply and ground; it is not a fabricated level translator.
        host_devices += [f'BhostLow{control} {output} 0 I=(1-v(bit{control}))*v({output})/50',
                         f'BhostHigh{control} {output} {node(vdd)} I=v(bit{control})*v({output},{node(vdd)})/50']
    deck = [lines[0], '* Host-only controls: BhostLow/BhostHigh, not part of the fabricated IC.',
            f'.lib "{models / "cornerMOShv.lib"}" {corner}',
            f'.lib "{models / "cornerMOSlv.lib"}" {corner}', f'.temp {temperature}']
    for line in lines[1:]:
        if line.startswith(('.model CircuitModel ', 'Ndesign ', 'pre_osdi ')):
            continue
        if line == '.control':
            deck += resistors + devices + host_devices
        deck.append(line)
    result = '\n'.join(deck) + '\n'
    if any(token in result for token in ('CircuitModel', 'design.osdi', 'cpphdl_channel')):
        raise ValueError('generic receive model remains in live process deck')
    return result, inventory
