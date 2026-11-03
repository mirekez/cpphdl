"""Bind observations and R3 bodies in the composed native-clock PLL extraction."""
import re

from extract_analog import inventory
from extracted_connections import check_capacitor_connections
from reference_clock_extracted_process import bind_observations
from reference_clock_layout_process import PORTS
from resistor_body_extraction import adapt
from reduce_extracted_wires import reduce

# Counts before parallel-device simplification. The geometry fixture separately
# requires the normalized 259-device LVS inventory against the composed circuit.
EXPECTED = dict(sg13_lv_nmos=148, sg13_lv_pmos=142, sg13_hv_nmos=30,
                sg13_hv_pmos=12, rppd=8, cap_cmim=74, dpantenna=1)


def netlist(extraction, geometry, requested):
    if geometry.get('substrate_net') != 'VSS':
        raise ValueError('native PLL extraction requires the common VSS wafer binding')
    named = extraction / 'named'
    ext = (named / 'ExtractedAnalog.ext').read_text()
    reference = (named / 'capacitance.spice').read_text()
    text = (extraction / 'conserved.spice').read_text()
    if inventory(text, PORTS)['models'] != EXPECTED:
        raise ValueError('native PLL extracted finger or passive inventory changed')
    reference_nodes = set(PORTS)
    for line in reference.splitlines():
        fields = line.split(';')[0].split()
        if fields and fields[0].startswith(('R', 'C')):
            reference_nodes.update(fields[1:3])
        elif fields and fields[0].startswith('X'):
            index = next(i for i, token in enumerate(fields) if token in EXPECTED)
            reference_nodes.update(fields[1:index])
    probes, bindings = bind_observations(geometry, requested, ext, reference_nodes)
    text, bodies = adapt(reference, text, ext, PORTS)
    connections = check_capacitor_connections(text, PORTS)
    text, reduction = reduce(text, PORTS, [n for n in probes.values() if n not in PORTS])
    text = re.sub(r'^(X[^\n]+\bcap_cmim\b[^\n]*)$', r'\1 ic=0', text, flags=re.M)
    text = text.replace('.subckt ExtractedAnalog ', '.subckt PhysicalReferenceClock ', 1)
    return text, probes, dict(bindings=bindings, physical_devices=EXPECTED,
        resistor_body_partition=bodies, connections=connections, reduction=reduction)
