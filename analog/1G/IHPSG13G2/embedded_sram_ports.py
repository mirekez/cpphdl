"""Retain physically labeled SRAM outputs that deep extraction leaves internal.

An unused macro output may have its complete internal conductor but no exported
hierarchy pin. Promotion adds only an interface pin and a private parent stub.
Every reference use must already be an isolated one-terminal stub; a used or
shorted output is rejected. No conductor, device or existing endpoint changes.
"""
import re

import klayout.db as db


def output_labels(gds, macro):
    layout = db.Layout()
    layout.read(str(gds))
    cell = layout.cell(macro)
    layer = layout.find_layer(10, 25)  # IHP Metal2 labels used by these outputs.
    if cell is None or layer is None:
        raise ValueError('missing physical SRAM output labels')
    labels = [s.text.string for s in cell.shapes(layer).each() if s.is_text()
              and re.fullmatch(r'[AB]_DOUT<(?:[0-9]|1[0-5])>', s.text.string)]
    expected = {f'{port}_DOUT<{bit}>' for port in 'AB' for bit in range(16)}
    if len(labels) != 32 or set(labels) != expected:
        raise ValueError('physical SRAM output interface differs')
    return expected


def restore_unused_outputs(result, macro, labels, expected_banks=8):
    left, right = result.netlist(), result.reference
    a = next((c for c in left.each_circuit() if c.name.upper() == macro.upper()), None)
    b = next((c for c in right.each_circuit() if c.name.upper() == macro.upper()), None)
    if a is None or b is None:
        raise ValueError('missing SRAM circuit')
    existing = {p.name() for p in a.each_pin()}
    desired = {p.name() for p in b.each_pin()}
    if existing - desired:
        raise ValueError('unexpected extracted SRAM interface')
    missing = sorted(desired - existing)
    left_uses = [(c, s) for c in left.each_circuit() for s in c.each_subcircuit() if s.circuit_ref() == a]
    right_uses = [(c, s) for c in right.each_circuit() for s in c.each_subcircuit() if s.circuit_ref() == b]
    if len(left_uses) != expected_banks or len(right_uses) != expected_banks:
        raise ValueError('unexpected SRAM bank inventory')
    plan = []
    for name in missing:
        if name not in labels or not re.fullmatch(r'[AB]_DOUT<(?:[0-9]|1[0-5])>', name):
            raise ValueError('only physically labeled unused data outputs may be promoted')
        net = a.net_by_name(name)
        if net is None or net.pin_count() or net.terminal_count() or net.subcircuit_pin_count() != 1:
            raise ValueError('unused output lacks its isolated extracted internal driver')
        pin = b.pin_by_name(name)
        for parent, child in right_uses:
            stub = child.net_for_pin(pin.id())
            if (stub is None or stub.pin_count() or stub.terminal_count() or stub.subcircuit_pin_count() != 1):
                raise ValueError('reference output has an external connection: ' + name)
        stubs = []
        for parent, child in left_uses:
            key = '__CPPHDL_UNUSED_' + str(child.id()) + '_' + name
            if parent.net_by_name(key) is not None:
                raise ValueError('reserved unused-output stub already exists')
            stubs.append((parent, child, key))
        plan.append((name, net, stubs))
    # Validate every output before changing any interface. The new parent nets
    # have exactly one endpoint, so promotion cannot join existing conductors.
    for name, net, stubs in plan:
        endpoints = [(p.subcircuit().id(), p.pin_id()) for p in net.each_subcircuit_pin()]
        pin = a.create_pin(name)
        a.connect_pin(pin.id(), net)
        for parent, child, key in stubs:
            if child.net_for_pin(pin.id()) is not None:
                raise ValueError('new output pin unexpectedly has an external connection')
            stub = parent.create_net(key)
            child.connect_pin(pin.id(), stub)
            if stub.pin_count() or stub.terminal_count() or stub.subcircuit_pin_count() != 1:
                raise ValueError('promoted output is not an isolated hierarchy stub')
        if endpoints != [(p.subcircuit().id(), p.pin_id()) for p in net.each_subcircuit_pin()]:
            raise ValueError('promotion changed the internal driver endpoint')
    if {p.name() for p in a.each_pin()} != desired:
        raise ValueError('incomplete promoted SRAM interface')
    return dict(promoted_outputs=missing, private_parent_stubs=len(missing) * len(left_uses),
                existing_conductors_preserved=True, qualified=False)
