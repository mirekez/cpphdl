"""Realize the actual cold receiver sampler with its retained hold and coupling charge."""
import json
import math
from pathlib import Path
import re

from capacitor_layout import append_mim
from capacitor_series_layout import append_mim_series
from mos_layout import append_mos
from oscillator_passive_layout import append_one_ff
from reference_clock_row import create_row
from receive_sampler_shields import add_sampler_shields, expose_held_plate

PORTS = ['VDD', 'VSS', 'IN', 'CLOCK', 'OUT']
TOP = 'ReceiveSampler'


def circuit(source, manifest):
    text = Path(source).read_text()
    metadata = json.loads(Path(manifest).read_text())
    inverse = metadata['clock_inverse']
    identities = metadata['selected_contributions']
    if (not re.fullmatch(r'cpphdl_n\d+', inverse) or
            not isinstance(identities, list) or len(identities) != 11 or
            any(type(i) is not int or not 0 <= i < metadata['parent_contributions'] for i in identities) or
            sorted(set(identities)) != identities or
            'module ReceiveSampler(' + ', '.join(PORTS) + ');' not in text):
        raise ValueError('changed sampler boundary or parent ownership')
    records = re.findall(r'^\s*// device (\d+), IC ([^,]+), ([^\n]+):\d+\n[ \t]*(I\([^\n]+)', text, re.M)
    if (len(records) != 11 or [int(r[0]) for r in records] != list(range(11)) or
            len(re.findall(r'^\s*I\(', text, re.M)) != 11):
        raise ValueError('missing, duplicate or unowned sampler contribution')
    if len({r[1] for r in records[:2]}) != 1 or len({r[1] for r in records[2:]}) != 1 or records[0][1] == records[2][1]:
        raise ValueError('sampler complement ownership changed')
    node = lambda name: 'CLOCK_N' if name == inverse else name
    # Geometry matches the reviewed differential_conversion_process binding.
    # D/S orientation and dummy gates are significant for switching charge.
    channels = {
        0: ('CLOCK_N', 'CLOCK', 'VDD', .004, -1, 20, 2),
        1: ('CLOCK_N', 'CLOCK', 'VSS', .004, 1, 10, 1),
        3: ('IN', 'CLOCK', 'OUT', .03, 1, 37.5, 5),
        4: ('OUT', 'CLOCK_N', 'IN', .015, -1, 75, 10),
        5: ('OUT', 'CLOCK_N', 'OUT', .016, 1, 20, 2),
        6: ('OUT', 'CLOCK', 'OUT', .0084, -1, 42, 5),
    }
    storage = {2: ('CLOCK_N', 'VSS', 20e-15), 7: ('OUT', 'VSS', 1.5e-12),
               8: ('CLOCK', 'OUT', 2e-15), 9: ('CLOCK_N', 'OUT', 2e-15),
               10: ('IN', 'OUT', 1e-15)}
    devices, capacitors = [], []
    for index, (_, _, origin, law) in enumerate(records):
        if Path(origin).name != ('inverter.hpp' if index < 2 else 'mos_track_hold.hpp'):
            raise ValueError('sampler declaration owner changed')
        if index in channels:
            match = re.fullmatch(r'I\((\w+), (\w+)\) <\+ cpphdl_channel\(V\((\w+), (\w+)\), '
                r'V\((\w+), (\w+)\), ([-+0-9.eE]+), ([-+0-9.eE]+), ([-+0-9.eE]+)\);', law)
            if not match:
                raise ValueError('unknown sampler channel law')
            a, b, gate, gb, da, db, beta, threshold, sign = match.groups()
            want_a, want_gate, want_b, want_beta, polarity, width, fingers = channels[index]
            if ((a, b, b) != (da, db, gb) or
                    (node(a), node(gate), node(b)) != (want_a, want_gate, want_b) or
                    any(not math.isclose(float(x), y, rel_tol=1e-12, abs_tol=0)
                        for x, y in zip((beta, threshold, sign), (want_beta, .7, polarity)))):
                raise ValueError('changed sampler channel topology or parameters')
            devices.append(dict(element=identities[index], index=index, a=want_a, gate=want_gate, b=want_b,
                body='VSS' if polarity == 1 else 'VDD', polarity='n' if polarity == 1 else 'p',
                width_um=width, length_um=.45, fingers=fingers))
        else:
            match = re.fullmatch(r'I\((\w+), (\w+)\) <\+ ([-+0-9.eE]+) \* ddt\(V\((\w+), (\w+)\)\); '
                r'// Bench initial voltage = ([-+0-9.eE]+) V\.', law)
            if not match:
                raise ValueError('unknown sampler storage law')
            a, b, value, va, vb, initial = match.groups()
            want_a, want_b, want_value = storage[index]
            if ((a, b) != (va, vb) or (node(a), node(b)) != (want_a, want_b) or
                    not math.isclose(float(value), want_value, rel_tol=1e-12, abs_tol=0) or float(initial) != 0):
                raise ValueError('changed sampler storage topology, value or cold state')
            capacitors.append(dict(element=identities[index], index=index,
                                   a=want_a, b=want_b, capacitance_F=want_value))
    return dict(devices=devices, capacitors=capacitors, parent_contributions=metadata['parent_contributions'],
                owned_contributions=identities, scope='literal sampler only; parent loads and clock source are external')


def create_receive_sampler(pdk, source, manifest):
    plan = circuit(source, manifest)

    def parts(layout):
        result = []
        for device in plan['devices']:
            cell, info = append_mos(layout, pdk, 'SamplerMOS' + str(device['index']),
                device['width_um'], device['length_um'], device['fingers'], device['polarity'], 'hv')
            result.append((cell, info, dict(D=device['a'], G=device['gate'], S=device['b'], B=device['body']), 'mos'))
        for capacitor in plan['capacitors']:
            value = capacitor['capacitance_F']
            make = append_one_ff if value < 2e-15 else append_mim_series if value < 3e-15 else append_mim
            cell, info = make(layout, pdk, 'SamplerStorage' + str(capacitor['index']), value)
            result.append((cell, info, dict(PLUS=capacitor['a'], MINUS=capacitor['b']), 'capacitor'))
        return result

    # These nets carry sampling charge, not line-driver current. Broad parallel
    # buses couple the closing clock into the held sample; retain the broad
    # supply rails but use narrow signal tracks and parallel via cuts.
    layout, top, geometry = create_row(TOP, PORTS, parts, priority_nets=('OUT', 'IN', 'CLOCK_N'),
                                      via_columns=2, signal_width_um=.26)
    add_sampler_shields(layout, top, geometry)
    expose_held_plate(layout, top, geometry)
    geometry['binding'] = plan
    return layout, top, geometry
