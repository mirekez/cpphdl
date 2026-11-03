"""Verify an SRAM LVS mapping against every simplified circuit endpoint.

Mapping hints can guide a search, but do not prove connectivity. This audit
checks the resulting bijections independently. Its default device set is the
SRAM's four-terminal MOS and two-terminal metal resistors. Assembly checks can
explicitly enable the two IHP antenna diode models. Other devices fail closed.
Diffusion parasitics require separate extracted electrical checks.
"""
import math
from pathlib import Path
import re

import klayout.db as db


def sheet_resistance(rule_deck):
    """Read nominal metal sheet resistances from the actual extraction deck."""
    values = re.findall(r'^RSH_RES_METAL([1-3])\s*=\s*([\d.eE+-]+)\s*$',
                        Path(rule_deck).read_text(), re.M)
    if len(values) != 3 or {n for n, _ in values} != {'1', '2', '3'}:
        raise ValueError('missing or ambiguous metal sheet resistance')
    result = {'res_metal' + n: float(v) for n, v in values}
    if any(not math.isfinite(v) or v <= 0 for v in result.values()):
        raise ValueError('invalid metal sheet resistance')
    return result


def audit(result, top_name, expected_ports, *, metal_sheet_resistance=None,
          allow_antenna_diodes=False):
    # Read endpoints from the supplied netlists. Cross-reference objects supply
    # correspondence only; they can belong to an older netlist snapshot.
    layout, reference = result.netlist(), result.reference
    layout_circuits = {c.name: c for c in layout.each_circuit()}
    reference_circuits = {c.name: c for c in reference.each_circuit()}
    xref = result.xref()
    pairs = list(xref.each_circuit_pair())
    if not pairs or any(p.first() is None or p.second() is None or
                        p.status() != db.NetlistCrossReference.Match for p in pairs):
        raise ValueError('all circuits must have a complete LVS match')
    left_circuits = set(layout_circuits)
    right_circuits = set(reference_circuits)
    if ({p.first().name for p in pairs} != left_circuits or
            {p.second().name for p in pairs} != right_circuits or
            len(pairs) != len(left_circuits) or len(pairs) != len(right_circuits)):
        raise ValueError('circuit mapping is not a complete bijection')
    tops = [p for p in pairs if p.first().name.upper() == top_name.upper()]
    if len(tops) != 1:
        raise ValueError('missing or ambiguous SRAM top')
    expected = {p.upper() for p in expected_ports}
    circuit_map, pin_maps, net_maps = {}, {}, {}
    counts = dict(circuits=len(pairs), devices=0, nets=0, pins=0, subcircuits=0,
                  derived_metal_resistors=0)

    def net_key(net):
        return None if net is None else net.expanded_name()

    def bijection(items, left_ids, right_ids, identify, label):
        if len(left_ids) != len(set(left_ids)) or len(right_ids) != len(set(right_ids)):
            raise ValueError('ambiguous ' + label + ' identifiers')
        mapping, used = {}, set()
        for pair in items:
            a, b = pair.first(), pair.second()
            if a is None or b is None:
                raise ValueError('missing ' + label + ' correspondence')
            i, j = identify(a), identify(b)
            if i in mapping or j in used:
                raise ValueError('duplicate ' + label + ' correspondence')
            mapping[i] = j
            used.add(j)
        if set(mapping) != set(left_ids) or set(mapping.values()) != set(right_ids):
            raise ValueError('incomplete ' + label + ' correspondence')
        return mapping

    for pair in pairs:
        left = layout_circuits[pair.first().name]
        right = reference_circuits[pair.second().name]
        circuit_map[left.name] = right.name
        pin_maps[left.name] = bijection(
            xref.each_pin_pair(pair), [p.id() for p in left.each_pin()],
            [p.id() for p in right.each_pin()], lambda p: p.id(), 'pin')
        net_maps[left.name] = bijection(
            xref.each_net_pair(pair), [net_key(n) for n in left.each_net()],
            [net_key(n) for n in right.each_net()], net_key, 'net')
        counts['pins'] += left.pin_count()
        counts['nets'] += len(net_maps[left.name])

    for pair in pairs:
        left = layout_circuits[pair.first().name]
        right = reference_circuits[pair.second().name]
        mapping = net_maps[left.name]
        left_pins = {p.id(): p for p in left.each_pin()}
        right_pins = {p.id(): p for p in right.each_pin()}

        def mapped(net):
            key = net_key(net)
            if key is None:
                return None
            if key not in mapping:
                raise ValueError('unmapped connected net')
            return mapping[key]

        for pin in xref.each_pin_pair(pair):
            a = left_pins[pin.first().id()]
            b = right_pins[pin.second().id()]
            if mapped(left.net_for_pin(a)) != net_key(right.net_for_pin(b)):
                raise ValueError('pin endpoint differs')
            if pair.first().name == tops[0].first().name and a.name().upper() != b.name().upper():
                raise ValueError('top port names differ')
        if pair.first().name == tops[0].first().name:
            for c in [left, right]:
                if c.pin_count() != len(expected) or {p.name().upper() for p in c.each_pin()} != expected:
                    raise ValueError('top interface differs from contract')

        devices = list(xref.each_device_pair(pair))
        left_devices = {d.id(): d for d in left.each_device()}
        right_devices = {d.id(): d for d in right.each_device()}
        bijection(devices, list(left_devices), list(right_devices),
                  lambda d: d.id(), 'device')
        for device in devices:
            a = left_devices[device.first().id()]
            b = right_devices[device.second().id()]
            model = a.device_class().name.lower()
            if model != b.device_class().name.lower():
                raise ValueError('device models differ')
            if model in ('sg13_lv_nmos', 'sg13_lv_pmos'):
                fixed, symmetric, parameters = ('G', 'B'), ('S', 'D'), ('L', 'W')
            elif model in ('res_metal1', 'res_metal2', 'res_metal3'):
                fixed, symmetric, parameters = (), ('A', 'B'), ('R', 'L', 'W')
                if not math.isfinite(a.parameter('R')) or a.parameter('R') <= 0:
                    raise ValueError('missing positive extracted metal resistance')
            elif allow_antenna_diodes and model in ('dantenna', 'dpantenna'):
                fixed, symmetric, parameters = ('A', 'C'), (), ('A', 'P', 'm')
                if any(not math.isfinite(d.parameter(p)) or d.parameter(p) <= 0
                       for d in (a, b) for p in parameters):
                    raise ValueError('missing positive antenna diode geometry/multiplicity')
                counts['antenna_diodes'] = counts.get('antenna_diodes', 0) + 1
            else:
                raise ValueError('unsupported SRAM model: ' + model)
            terminals = set(fixed + symmetric)
            if any({t.name for t in d.device_class().terminal_definitions()} != terminals for d in [a, b]):
                raise ValueError('unexpected device terminals')
            for name in parameters:
                v, w = a.parameter(name), b.parameter(name)
                if (name == 'R' and w == 0 and
                        not a.device_class().parameter_definition('R').is_primary and
                        all(a.device_class().parameter_definition(p).is_primary for p in ('L', 'W'))):
                    # The PDK CDL reader leaves R at zero for geometry-defined
                    # metal resistors. Check the extracted R against Rsh * L/W,
                    # rather than treating that placeholder as zero resistance.
                    sheet = (metal_sheet_resistance or {}).get(model)
                    length, width = b.parameter('L'), b.parameter('W')
                    if sheet is None or not math.isfinite(sheet) or sheet <= 0 or length <= 0 or width <= 0:
                        raise ValueError('geometry-defined resistance needs a valid extraction model')
                    w = sheet * length / width
                    counts['derived_metal_resistors'] += 1
                # EnDiode in the IHP extraction deck specifies 1% A/P tolerance.
                # Its terminal polarity and multiplicity must still match.
                tolerance = .01 if model in ('dantenna', 'dpantenna') and name in ('A', 'P') else 1e-10
                if not (math.isfinite(v) and math.isfinite(w) and
                        math.isclose(v, w, rel_tol=tolerance, abs_tol=1e-12)):
                    raise ValueError('device parameter differs: ' + model + '.' + name)
            for name in fixed:
                if mapped(a.net_for_terminal(name)) != net_key(b.net_for_terminal(name)):
                    raise ValueError('device fixed terminal differs: ' + model + '.' + name)
            before = [mapped(a.net_for_terminal(n)) for n in symmetric]
            after = [net_key(b.net_for_terminal(n)) for n in symmetric]
            if before != after and before != after[::-1]:
                raise ValueError('device symmetric terminals differ')
            counts['devices'] += 1

        children = list(xref.each_subcircuit_pair(pair))
        left_children = {s.id(): s for s in left.each_subcircuit()}
        right_children = {s.id(): s for s in right.each_subcircuit()}
        bijection(children, list(left_children), list(right_children),
                  lambda s: s.id(), 'subcircuit')
        for child in children:
            a = left_children[child.first().id()]
            b = right_children[child.second().id()]
            target = a.circuit_ref().name
            if circuit_map.get(target) != b.circuit_ref().name:
                raise ValueError('subcircuit target differs')
            for i, j in pin_maps[target].items():
                if mapped(a.net_for_pin(i)) != net_key(b.net_for_pin(j)):
                    raise ValueError('subcircuit pin endpoint differs')
            counts['subcircuits'] += 1
    return dict(status='pass', scope='complete simplified SRAM graph correspondence',
                counts=counts, metal_sheet_resistance=metal_sheet_resistance,
                full_sram_qualified=False, full_chip_qualified=False)
