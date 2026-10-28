"""Connect the physical oscillator and admission gate to the live controller.

The two extracted macros keep separate internal nodes and share their actual
supply and raw-clock terminals. This is a macro interconnection experiment;
the inter-macro routes must still be extracted from the assembled GDS.
"""
import re

from extract_analog import inventory
from extracted_connections import check_capacitor_connections
from oscillator_extracted_process import netlist as oscillator_netlist
from processing_clock_gate_layout import circuit as gate_circuit
from processing_clock_oscillator_layout import PORTS
from reduce_extracted_wires import reduce

GATE_PORTS = ['VDD', 'VSS', 'CLOCK', 'ENABLE', 'OUT']


def gate_netlist(extraction, model):
    devices, _, _ = gate_circuit(model)
    driver_inputs = {d['gate'] for d in devices if d['a'] == 'OUT' and d['role'].startswith('strong_')}
    if len(driver_inputs) != 1:
        raise ValueError('ambiguous gate output driver')
    nand_output = next(iter(driver_inputs))
    held = [(i, d) for i, d in enumerate(devices) if d['a'] == nand_output
            and d['role'] == 'logic_p' and d['gate'] != 'CLOCK']
    if len(held) != 1:
        raise ValueError('ambiguous physical admission-state observation')
    terminal = f'GateMOS{held[0][0]}_0.G'
    parent = {}

    def root(node):
        parent.setdefault(node, node)
        if parent[node] != node:
            parent[node] = root(parent[node])
        return parent[node]

    text = (extraction / 'conserved.spice').read_text()
    ext = (extraction / 'ExtractedAnalog.ext').read_text()
    for a, b in re.findall(r'^equiv "([^"]+)" "([^"]+)"', ext, re.M):
        parent[root(b)] = root(a)
    actual = inventory(text, GATE_PORTS)
    expected = {'sg13_lv_nmos': 21, 'sg13_lv_pmos': 25, 'cap_cmim': 28}
    if actual['models'] != expected:
        raise ValueError('physical gate device inventory changed: ' + str(actual['models']))
    reference = inventory((extraction / 'capacitance.spice').read_text(), GATE_PORTS)
    nodes = {node for a, b, _ in reference['capacitors'] for node in (a, b)}
    candidates = [node for node in nodes if root(node) == root(terminal)]
    if len(candidates) != 1:
        raise ValueError('missing or ambiguous physical gate state')
    probe = candidates[0]
    connections = check_capacitor_connections(text, GATE_PORTS)
    reduced, audit = reduce(text, GATE_PORTS, [probe])
    reduced = re.sub(r'^(X[^\n]+\bcap_cmim\b[^\n]*)$', r'\1 ic=0', reduced, flags=re.M)
    reduced = reduced.replace('.subckt ExtractedAnalog ', '.subckt PhysicalGate ', 1)
    return reduced, probe, dict(physical_devices=expected, observation_terminal=terminal,
        observation_node=probe, reduction=audit, connections=connections)


def deck(oscillator_extraction, gate_extraction, oscillator_model, gate_model, models, stop_s):
    if stop_s not in (512e-9, 64e-6):
        raise ValueError('choose the startup diagnostic or complete 64 us controller experiment')
    oscillator, probes, oscillator_audit = oscillator_netlist(oscillator_extraction, oscillator_model)
    gate, held, gate_audit = gate_netlist(gate_extraction, gate_model)
    # The host outputs correspond to coarse[7:0], fine[7:0], CLEAR and ADMIT.
    # Finite 50-ohm simulation drivers are the same boundary as the schematic
    # delivery experiment. They are not physical controller output transistors.
    pins = [f'COARSE_N{bit}' for bit in range(8)] + [f'FINE_N{bit}' for bit in range(8)] + ['CLEAR', 'ADMIT']
    controls = []
    for bit, pin in enumerate(pins):
        controls += [f'Bdown{bit} {pin} 0 I=(1-v(bit{bit}))*v({pin})/50',
                     f'Bup{bit} {pin} VDD I=v(bit{bit})*v({pin},VDD)/50']
    oscillator_pins = ['VDD', '0', 'CLEAR', 'RAW', *PORTS[4:]]
    observations = ['v(RAW)', f'v(Xoscillator.{probes["RING"]})',
        f'v(Xoscillator.{probes["CONTROL"]})', 'v(VDD)', 'v(CLEAR)', 'v(DELIVERED)',
        'v(ADMIT)', f'v(Xgate.{held})']
    digital = ' '.join(f'host_bit{bit}' for bit in range(18))
    analog = ' '.join(f'bit{bit}' for bit in range(18))
    result = '\n'.join(['Extracted processing oscillator and admission gate with live calibration',
        f'.lib "{models}/cornerMOSlv.lib" mos_tt', f'.lib "{models}/cornerRES.lib" res_typ',
        f'.lib "{models}/cornerCAP.lib" cap_typ', f'.include "{models}/diodes.lib"', '.temp 27',
        oscillator, gate, 'Vsupply SUPPLY 0 1.2', 'Rsupply SUPPLY VDD 10',
        'Xoscillator ' + ' '.join(oscillator_pins) + ' PhysicalOscillator',
        'Xgate VDD 0 RAW ADMIT DELIVERED PhysicalGate',
        'Craw RAW 0 20f IC=0', 'Cload DELIVERED 0 200f IC=0', *controls,
        'Vreference REFERENCE 0 PULSE(0 1.2 4n 1p 1p 3.999n 8n)',
        'Aclocks [REFERENCE RAW] [ref_clock pcs_clock] host_input',
        f'Acontroller [ref_clock pcs_clock] [{digital}] null host_controller',
        f'Aoutputs [{digital}] [{analog}] host_output',
        '.model host_input adc_bridge(in_low=.36 in_high=.84 rise_delay=1p fall_delay=1p)',
        '.model host_output dac_bridge(out_low=0 out_high=1 out_undef=.5 t_rise=100p t_fall=100p)',
        '.model host_controller d_cosim(simulation="./controller.so" sim_args=["events.csv"] delay=1p)',
        '.save ' + ' '.join(observations),
        '.options klu method=gear maxord=2 reltol=1e-5 abstol=1e-12 vntol=1e-8 chgtol=1e-16',
        '.control', 'set num_threads=1', 'set noaskquit', 'set wr_singlescale', 'set wr_vecnames',
        'set numdgt=17', f'tran 5p {stop_s:.17g} 0 10p uic',
        'wrdata spice.dat ' + ' '.join(observations), 'quit', '.endc', '.end', ''])
    return result, dict(oscillator=oscillator_audit, gate=gate_audit, observations=observations,
        raw_external_load_F=20e-15, delivered_external_load_F=200e-15,
        scope='physical macro devices and internal RC, shared supply, live controller; '
              'ideal inter-macro connections and simulation output drivers; capacitance calibration pending')
