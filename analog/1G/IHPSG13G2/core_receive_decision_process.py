"""Explicit SG13G2 binding for the connected trial-to-decision circuit."""
from collections import Counter, defaultdict
import math
import re

from core_driver_process import contributions
from core_receive_dac_process import TABLE
from receive_frontend_process import match_parameters, selections


def process_deck(model, original, models, corner='mos_tt', temperature=27):
    if corner not in ('mos_tt', 'mos_ss', 'mos_ff') or not math.isfinite(temperature) or not -40 <= temperature <= 125:
        raise ValueError('unsupported decision corner or temperature')
    marker, suffix = '    analog begin\n', '    end\nendmodule\n'
    if model.count(marker) != 1 or not model.endswith(suffix):
        raise ValueError('changed decision contribution block')
    lines = original.splitlines()
    obs = dict(re.findall(r'^let observation(\d+) = v\((n\d+)\)$', original, re.M))
    if set(obs) != set(map(str, range(56))) or lines.count('.control') != 1 or sum(line.startswith('Ndesign ') for line in lines) != 1:
        raise ValueError('changed decision observation interface')
    vdd, core, bias = (obs[str(i)] for i in (10, 11, 12))
    annotation = re.compile(r'// device (\d+), IC ([^,]+), .*/([^/]+[.]hpp):\d+')
    channel = re.compile(r'I\((n\d+), (n\d+)\) <\+ cpphdl_channel\(V\((n\d+), (n\d+)\), V\((n\d+), (n\d+)\), ([-+0-9.eE]+), ([-+0-9.eE]+), ([-+0-9.eE]+)\);')
    resistor = re.compile(r'I\((n\d+), (n\d+)\) <\+ V\((n\d+), (n\d+)\) / ([-+0-9.eE]+);')
    groups, resistors, seen = defaultdict(list), [], set()
    owner = filename = identifier = None
    for line in model.split(marker, 1)[1].removesuffix(suffix).splitlines():
        line = line.strip()
        match = annotation.fullmatch(line)
        if match:
            identifier, owner, filename = match.groups()
        elif line.startswith('// device '):
            identifier = re.match(r'// device (\d+), ', line)[1]
            owner = filename = None
        if not line or line.startswith('//'):
            continue
        if identifier is None or identifier in seen:
            raise ValueError('unidentified or repeated decision device')
        seen.add(identifier)
        match = resistor.fullmatch(line)
        if match:
            a, b, va, vb, value = match.groups()
            if (a, b) != (va, vb) or not math.isfinite(float(value)):
                raise ValueError('changed decision resistor law')
            resistors.append((identifier, a, b, float(value)))
            continue
        match = channel.fullmatch(line)
        if not match or owner is None:
            raise ValueError('unbound decision law')
        a, b, gate, source, drain, drain_source, beta, threshold, polarity = match.groups()
        if (source, drain, drain_source) != (b, a, b):
            raise ValueError('changed decision MOS terminal references')
        groups[owner, filename].append(dict(id=identifier, a=a, b=b, gate=gate,
            parameters=tuple(map(float, (beta, threshold, polarity))), law=line))
    expected = Counter({('inverter.hpp', 2): 43, ('cascoded_level_shifter.hpp', 6): 10,
        ('cascoded_level_shifter.hpp', 1): 1, ('nmos_resistor_amplifier.hpp', 2): 1,
        ('strongarm_comparator.hpp', 11): 1, ('cmos_logic.hpp', 4): 2,
        ('nand_latch.hpp', 2): 1, ('level_down.hpp', 4): 1})
    if Counter((file, len(parts)) for (_, file), parts in groups.items()) != expected:
        raise ValueError('changed connected decision component inventory')
    if Counter(r[3] for r in resistors) != Counter({.25: 1, 10: 1, 50: 11, 200: 1, 800: 1,
            10000: 1, 750: 7, 1500: 9, 1000: 2, 100: 2, 300: 1}):
        raise ValueError('changed connected decision resistors')
    by_file = lambda file: [parts for (_, name), parts in groups.items() if name == file]
    preamp, = by_file('nmos_resistor_amplifier.hpp')
    comparator, = by_file('strongarm_comparator.hpp')
    interface, = by_file('level_down.hpp')
    if ([d['gate'] for d in preamp] != [obs['0'], obs['1']] or
            [comparator[i]['gate'] for i in range(3)] != [obs['2'], obs['3'], obs['5']] or
            [preamp[i]['a'] for i in range(2)] != [obs['3'], obs['2']] or
            interface[1]['gate'] != obs['8'] or interface[3]['a'] != obs['9'] or
            interface[0]['a'] != obs['15'] or interface[0]['b'] != core or interface[2]['b'] != core):
        raise ValueError('disconnected DAC, preamplifier, comparator or core return')
    translators = [parts for parts in by_file('cascoded_level_shifter.hpp') if len(parts) == 6]
    for i in range(10):
        control, complement, low, low_n = (obs[str(16 + 4 * i + k)] for k in range(4))
        selected = [parts for parts in translators if parts[5]['gate'] == control]
        if len(selected) != 1:
            raise ValueError('missing translated decision control')
        parts = selected[0]
        if (parts[4]['gate'] != complement or [parts[j]['b'] for j in (2, 3)] != [low, low_n] or
                any(parts[j]['gate'] != bias for j in (2, 3))):
            raise ValueError('changed shared bias or core-exposure probes')
        if i >= 8:
            output = obs['5' if i == 8 else '14']
            if control != obs['4' if i == 8 else '13'] or len([inv for inv in by_file('inverter.hpp')
                    if inv[0]['gate'] == parts[1]['a'] and inv[0]['a'] == output]) != 1:
                raise ValueError('clock or clear bypasses its physical translator')
    caps = [re.fullmatch(r'Cdevice(\d+) (\S+) (\S+) ([-+0-9.eE]+) IC=([-+0-9.eE]+)', line)
        for line in lines if line.startswith('Cdevice')]
    cap_ids = re.findall(r'// Capacitor device (\d+) is stamped once by the SPICE host deck[.]', model)
    if (len(caps) != 303 or any(m is None for m in caps) or
            Counter(m[1] for m in caps) != Counter(cap_ids) or
            any(not math.isfinite(float(m[4])) or float(m[4]) <= 0 or float(m[5]) != 0 for m in caps)):
        raise ValueError('changed or non-cold connected decision storage')
    node = lambda value: '0' if value == 'n0' else value
    devices = []
    for (owner, file), parts in groups.items():
        parameters = [part['parameters'] for part in parts]
        if file == 'cascoded_level_shifter.hpp' or (file == 'inverter.hpp' and parameters[0][0] in (.02, .006, .04)):
            bound, _ = contributions(marker + '\n'.join(part['law'] for part in parts) + '\n' + suffix, TABLE)
            widths = [d['width_um'] for d in bound]
        elif file == 'nmos_resistor_amplifier.hpp':
            if not match_parameters(parameters, [(.0075, .7, 1)] * 2):
                raise ValueError('changed decision preamplifier size')
            widths = [25, 25]
        else:
            widths = selections(file, parameters)
        for part, width in zip(parts, widths):
            _, threshold, polarity = part['parameters']
            lv = threshold in (.35, .5)
            kind = 'sg13_' + ('lv' if lv else 'hv') + ('_nmos' if polarity > 0 else '_pmos')
            devices.append(dict(id=part['id'], owner=owner, file=file, a=node(part['a']), b=node(part['b']),
                gate=node(part['gate']), body='0' if polarity > 0 else node(core if lv else vdd),
                model=kind, width_um=width, length_um=.13 if lv else .45))
    deck = [lines[0], f'.lib "{models / "cornerMOSlv.lib"}" {corner}',
        f'.lib "{models / "cornerMOShv.lib"}" {corner}', f'.temp {temperature}']
    for line in lines[1:]:
        if line.startswith(('.model CircuitModel ', 'Ndesign ', 'pre_osdi ')):
            continue
        if line == '.control':
            deck += [f'Rphysical{i} {node(a)} {node(b)} {value:.17g}' for i, a, b, value in resistors]
            deck += [f'Xphysical{d["id"]} {d["a"]} {d["gate"]} {d["b"]} {d["body"]} {d["model"]} '
                f'w={d["width_um"]}u l={d["length_um"]}u' + (' ng=5' if d['width_um'] == 25 else '') for d in devices]
        deck.append(line)
    return '\n'.join(deck) + '\n', dict(devices=devices, resistors=len(resistors), capacitors=len(caps),
        scope='physical trial DAC, shared bias, evaluation/clear translation, preamplifier, comparator, retained decision and core return; driven input boundary')
