"""Connect C++ current-cell boundaries to their measured physical conductors."""
from collections import Counter
import math
from pathlib import Path
import re

from cascoded_driver_process import BINDINGS
from core_driver_process import contributions

PORTS = ['P', 'N', 'GP', 'GN', 'BIAS', 'CASCODE', 'VSS', 'TAIL', 'LOWER']


def bind_current_cell(source, geometry, history=False):
    """Check the exported four laws against the actual guarded layout banks."""
    text = Path(source).read_text()
    if 'module OneGTransmitCurrentCell(' + ', '.join(PORTS) + ');' not in text:
        raise ValueError('unexpected current-cell export interface')
    names = ['VSS', *[p for p in PORTS if p != 'VSS']]
    mapping = {p: 'n' + str(i) for i, p in enumerate(names)}
    text = re.sub(r'\b(?:' + '|'.join(names) + r')\b', lambda m: mapping[m[0]], text)
    devices, roles = contributions(text, BINDINGS)
    prefix = 'history' if history else 'main'
    if roles != Counter({prefix + '_steering': 2, prefix + '_current_cascode': 1, prefix + '_sink': 1}):
        raise ValueError('current-cell process roles changed')
    rename = {v: k for k, v in mapping.items()} | {'0': 'VSS'}
    expected = [dict(D='P', G='GP', S='TAIL', B='VSS'),
                dict(D='N', G='GN', S='TAIL', B='VSS'),
                dict(D='TAIL', G='CASCODE', S='LOWER', B='VSS'),
                dict(D='LOWER', G='BIAS', S='VSS', B='VSS')]
    if (len(devices) != 4 or len(geometry['devices']) != 4 or geometry['history'] != history or
            set(geometry['terminals']) != set(PORTS[:7]) or set(geometry['internal_anchors']) != {'TAIL', 'LOWER'}):
        raise ValueError('current-cell geometry boundary changed')
    for device, physical, nodes in zip(devices, geometry['devices'], expected):
        actual = {p: rename[device[k]] for p, k in zip(('D', 'G', 'S', 'B'), ('a', 'gate', 'b', 'body'))}
        if actual != nodes or physical['nets'] != nodes:
            raise ValueError('current-cell cascode or steering connection changed')
        bank = physical['geometry']
        if (device['model'] != 'sg13_hv_nmos' or
                not math.isclose(device['width_um'], bank['total_width_um'], rel_tol=1e-12) or
                not math.isclose(device['length_um'], bank['length_um'], rel_tol=1e-12)):
            raise ValueError('C++ process binding differs from physical current-cell dimensions')
    return devices


def expose_storage_anchors(text, name):
    """Expose existing RC nodes; never join distributed transistor terminals.

    The macro's measured internal anchors become the terminals for the retained
    C++ storage. Chip assembly must physically connect storage at these anchors;
    adding SPICE ports alone does not establish a complete routed layout.
    """
    if not re.fullmatch(r'[A-Za-z][A-Za-z0-9_]*', name):
        raise ValueError('invalid current-cell model name')
    header = '.subckt ExtractedAnalog ' + ' '.join(PORTS[:7])
    if len(re.findall(r'^' + re.escape(header) + r'$', text, re.M)) != 1:
        raise ValueError('unexpected original extracted current-cell ports')
    ends = re.findall(r'^\.ends(?:\s+ExtractedAnalog)?\s*$', text, re.M | re.I)
    if len(ends) != 1:
        raise ValueError('unexpected extracted current-cell model boundary')
    # Names in capacitor records alone do not prove a physical wire connection.
    # Each anchor must land on a positive finite extracted wire resistor.
    connected = set()
    for line in text.splitlines():
        fields = line.split()
        if fields and fields[0].startswith('R'):
            if len(fields) != 4 or not math.isfinite(float(fields[3])) or float(fields[3]) <= 0:
                raise ValueError('invalid current-cell extracted wire')
            connected.update(set(fields[1:3]) & {'TAIL', 'LOWER'})
    if connected != {'TAIL', 'LOWER'}:
        raise ValueError('missing physically connected current-cell storage anchor')
    result = text.replace(header, '.subckt ' + name + ' ' + ' '.join(PORTS), 1)
    result = re.sub(r'^\.ends(?:\s+ExtractedAnalog)?\s*$', '.ends ' + name, result, flags=re.M | re.I)
    return result.rstrip('\n') + '\n'
