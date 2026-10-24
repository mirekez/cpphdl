"""Trial both bias-storage realizations using actual MOS gate charge.

The reference C++ equations retain their nominal 400/50 pF storage. PSP uses
forty dedicated MOSCAP cells for the low-voltage current bias and 220 small
HV MOS units for the cascode bias. Smaller repeated units avoid the high gate
resistance of a single very wide device. These are bias decouplers, not ADC
precision capacitors. Model selection does not establish extracted performance.
"""
import math

from moscap_driver_process import process_deck as bias_deck


def process_deck(model, original, models, corner, *, programmable=True, temperature=27):
    deck, bindings = bias_deck(model, original, models, corner,
                              programmable=programmable, temperature=temperature)
    gate = {d['gate'] for d in bindings['devices']
            if d.get('role') in ('main_current_cascode', 'history_current_cascode')}
    if len(gate) != 1:
        raise ValueError('MOS cascode storage requires one bias node')
    gate = next(iter(gate))
    targets = [line for line in deck.splitlines() if line.startswith('Cdevice')
               and line.split()[1:3] == [gate, '0']
               and math.isclose(float(line.split()[3]), 50e-12, rel_tol=1e-12)]
    fields = targets[0].split() if len(targets) == 1 else []
    if len(fields) != 5 or not fields[4].startswith('IC=') or float(fields[4][3:]) != 0:
        raise ValueError('trial may replace only the identified cold-start 50 pF cascode storage')
    deck = deck.replace(targets[0], f'XcascodeStorage 0 {gate} 0 0 sg13_hv_nmos w=100u l=.5u ng=10 m=220', 1)
    if targets[0] in deck or deck.count('XcascodeStorage ') != 1:
        raise ValueError('cascode storage omitted or double counted')
    bindings['cascode_storage'] = dict(realization='sg13_hv_nmos', node=gate, reference='0',
        width_um=100, length_um=.5, fingers=10, copies=220, gate_area_um2=11000,
        replaced_nominal_capacitance_F=50e-12, corner=corner, voltage_limit_V=2.4,
        drain_source_body='ground')
    bindings['scope'] += '; 50 pF cascode storage replaced by 220 parallel ten-finger HV MOS units'
    return deck, bindings
