"""Trial MOSCAP realization of the C++ transmitter's nominal bias storage.

The C++ reference still declares 400 pF as an approximate storage law. Only
that identified capacitor is replaced; actual MOSCAP charge and leakage are
then solved by PSP. This is an explicit process experiment, not an identical
equation export or an adopted physical implementation.
"""
import math
import re

from cascoded_driver_process import process_deck as reference_deck


def process_deck(model, original, models, corner, *, programmable=True, temperature=27):
    deck, bindings = reference_deck(model, original, models, corner,
                                    programmable=programmable, temperature=temperature)
    bias = {d['gate'] for d in bindings['devices'] if d.get('role') in ('main_sink', 'history_sink')}
    if len(bias) != 1:
        raise ValueError('MOSCAP storage requires one shared current bias')
    bias = next(iter(bias))
    targets = [line for line in deck.splitlines() if line.startswith('Cdevice')
               and line.split()[1:3] == [bias, '0']
               and math.isclose(float(line.split()[3]), 400e-12, rel_tol=1e-12)]
    fields = targets[0].split() if len(targets) == 1 else []
    if len(fields) != 5 or not fields[4].startswith('IC=') or float(fields[4][3:]) != 0:
        raise ValueError('trial may replace only the identified cold-start 400 pF bias storage')
    deck = deck.replace(targets[0], f'XbiasStorage {bias} 0 sg13_moscap_n w=30u l=30u m=40', 1)
    deck = deck.replace(f'.temp {temperature}',
                        f'.lib "{models / "cornerMOSCAP.lib"}" moscap_tt\n.temp {temperature}', 1)
    if len(re.findall(r'^XbiasStorage ', deck, re.M)) != 1 or targets[0] in deck:
        raise ValueError('MOSCAP was omitted or double counted with ideal storage')
    bindings['bias_storage'] = dict(realization='sg13_moscap_n', node=bias, reference='0',
        width_um=30, length_um=30, copies=40, replaced_nominal_capacitance_F=400e-12,
        gate_area_um2=36000, process_corner='moscap_tt', voltage_limit_V=1.3,
        scope='trial process realization with actual voltage-dependent charge and leakage; other explicit passives retained')
    bindings['scope'] += '; one ideal 400 pF bias capacitor replaced by forty actual 30 x 30 um MOSCAPs'
    return deck, bindings
