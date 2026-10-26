"""Reject capacitive extraction nodes disconnected from their intended devices."""
from extract_analog import inventory


def check_capacitor_connections(text, ports):
    """Check an intentional-device macro without floating fill islands.

    Magic issue #559 can leave coupling connected to resistor-body node names
    which have no corresponding device terminal in the SPICE output. Numeric
    capacitance conservation alone cannot detect that electrical disconnect.
    MIM series junctions remain valid: they are real device terminals.
    """
    net = inventory(text, ports)
    attached = set(ports)
    for a, b, _ in net['resistors']:
        attached.update((a, b))
    records = []
    for line in text.splitlines():
        if line.lstrip().startswith('+'):
            records[-1] += ' ' + line.lstrip()[1:]
        else:
            records.append(line)
    for line in records:
        fields = line.split(';')[0].split()
        if fields and fields[0].startswith('X'):
            model = next(i for i, token in enumerate(fields) if token in net['models'])
            attached.update(fields[1:model])
    endpoints = {node for a, b, value in net['capacitors'] if value > 0 for node in (a, b)}
    floating = sorted(endpoints - attached)
    if floating:
        raise ValueError(f'{len(floating)} capacitor endpoints have no wire/device/port connection: '
                         + ', '.join(floating[:8]))
    return dict(checked_nonzero_capacitor_endpoints=len(endpoints), unattached_endpoints=0,
                scope='intentional-device macro; floating fill structures require a separate model')
