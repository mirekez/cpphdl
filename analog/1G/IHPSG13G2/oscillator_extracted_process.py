"""Bind oscillator observations to the actual extracted physical connectivity."""
import re

from extract_analog import inventory
from extracted_connections import check_capacitor_connections
from resistor_body_extraction import adapt
from processing_clock_oscillator_layout import circuit, PORTS
from reduce_extracted_wires import reduce


def netlist(extraction, model):
    devices, _ = circuit(model)
    ring = [i for i, d in enumerate(devices) if d.get('role') == 'predriver_n']
    if len(ring) != 1:
        raise ValueError('ambiguous predriver observation')
    requested = {'RING': f'OscillatorMOS{ring[0]}_0.G', 'CONTROL': 'ControlAntenna_0.PLUS'}
    parent = {}

    def root(node):
        parent.setdefault(node, node)
        if parent[node] != node:
            parent[node] = root(parent[node])
        return parent[node]

    ext = (extraction / 'ExtractedAnalog.ext').read_text()
    for a, b in re.findall(r'^equiv "([^"]+)" "([^"]+)"', ext, re.M):
        parent[root(b)] = root(a)
    text = (extraction / 'conserved.spice').read_text()
    original = inventory(text, PORTS)
    expected = {'sg13_lv_nmos': 27, 'sg13_lv_pmos': 28, 'rppd': 33,
                'rhigh': 32, 'cap_cmim': 60, 'dpantenna': 1}
    if original['models'] != expected:
        raise ValueError('extracted oscillator device inventory changed')
    text, body_partition = adapt((extraction / 'capacitance.spice').read_text(), text, ext, PORTS)
    connections = check_capacitor_connections(text, PORTS)
    reference = inventory((extraction / 'capacitance.spice').read_text(), PORTS)
    reference_nodes = {n for a, b, _ in reference['capacitors'] for n in (a, b)}
    probes = {}
    for name, terminal in requested.items():
        candidates = [n for n in reference_nodes if root(n) == root(terminal)]
        if len(candidates) != 1:
            raise ValueError('missing or ambiguous extracted observation: ' + terminal)
        probes[name] = candidates[0]
    reduced, audit = reduce(text, PORTS, probes.values())
    # The maintained bench declares initially discharged physical storage.
    # Preserve every extracted device dimension and diffusion parameter.
    reduced = re.sub(r'^(X[^\n]+\bcap_cmim\b[^\n]*)$', r'\1 ic=0', reduced, flags=re.M)
    reduced = reduced.replace('.subckt ExtractedAnalog ', '.subckt PhysicalOscillator ', 1)
    return reduced, probes, dict(reduction=audit, connections=connections, physical_devices=expected,
        resistor_body_partition=body_partition,
        observation_terminals=requested, observation_logical_nodes=probes)
