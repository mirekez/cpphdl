"""Compose a loading experiment from the actual TX's exported IC boundaries."""
import json
import math
from pathlib import Path
import re

from transmit_translator_layout import PORTS, realization
from transmit_translation_bias_layout import circuit as bias_circuit


def signature(devices, capacitors):
    # Device emission order is retained from C++. Canonicalize private nodes,
    # whose numeric identities depend on their parent simulation's other ICs.
    names = {p: p for p in PORTS}
    def node(name):
        if name not in names:
            names[name] = 'private_' + str(len(names))
        return names[name]
    graph = []
    for device in devices:
        graph.append((device['role'], device['model'], device['width_um'], device['length_um'],
                      *(node(device[p]) for p in ('a', 'gate', 'b', 'body'))))
    charge = sorted((tuple(sorted((node(a), node(b)))), value) for a, b, value in capacitors)
    return graph, charge


def validate_export(directory, standalone, replica_source):
    manifest = json.loads((directory / 'bank.json').read_text())
    gates = manifest['translators']
    if (len(gates) != 22 or manifest['replica_elements'] != 5 or
            not math.isclose(manifest['storage_F'], 50e-12, rel_tol=1e-12) or
            any(g['bias_node'] != manifest['bias_node'] or g['elements'] != 36 for g in gates) or
            len({g['input_node'] for g in gates}) != 22 or len({g['output_node'] for g in gates}) != 22):
        raise ValueError('changed translator bank sharing or inventory')
    reference = signature(*realization(standalone, 'psp-charge')[:2])
    for index, gate in enumerate(gates):
        if gate['file'] != f'translator_{index}.va':
            raise ValueError('bank export file order changed')
        if signature(*realization(directory / gate['file'], 'psp-charge')[:2]) != reference:
            raise ValueError('actual bank translator differs from the physical macro')
    # Use the replica parser's circuit and value checks on both independent
    # exports; the schematic copy must describe the TX's actual instance.
    if bias_circuit(directory / 'translation_bias.va') != bias_circuit(replica_source):
        raise ValueError('actual bank replica differs from the physical macro')
    return manifest


def with_exposure_probe(model, name):
    pairs = set()
    for line in model.splitlines():
        fields = line.split()
        if len(fields) >= 6 and fields[0].startswith('X') and fields[5] in ('sg13_lv_nmos', 'sg13_lv_pmos'):
            pairs.update((fields[2], fields[i]) for i in (1, 3, 4))
    if not pairs or 'bank_lv_exposure' in model:
        raise ValueError('missing or ambiguous LV exposure observation')
    terms = [f'abs(v({a},{b}))' for a, b in sorted(pairs)]
    # An isolated zero-load observer records the maximum gate-to-terminal
    # voltage of every actual LV finger. This bounds trace storage without
    # omitting startup or choosing just one finger's private RC terminal.
    while len(terms) > 1:
        terms = [f'max({terms[i]},{terms[i + 1]})' if i + 1 < len(terms) else terms[i]
                 for i in range(0, len(terms), 2)]
    pattern = re.compile(r'^\.ends(?:[ \t]+' + re.escape(name) + r')?[ \t]*$', re.I | re.M)
    if len(list(pattern.finditer(model))) != 1:
        raise ValueError('unexpected translator subcircuit boundary')
    return pattern.sub('Bbank_lv_exposure bank_lv_exposure 0 V={' + terms[0] + '}\n.ends ' + name, model)


def deck(translator, translator_name, replica, replica_name, models, corner='mos_tt', temperature=27):
    if corner not in ('mos_tt', 'mos_ss', 'mos_ff') or not -40 <= temperature <= 125:
        raise ValueError('unsupported bank corner')
    wave, previous = [(0, 0)], 0
    for symbol in range(1, 16):
        value = 1.2 if symbol & 1 else 0
        wave += [(2e-6 + symbol * 8e-9, previous), (2e-6 + symbol * 8e-9 + .1e-9, value)]
        previous = value
    lines = ['* Actual 22-translator shared-bias bank; simultaneous switching stress',
        f'.lib "{models / "cornerMOSlv.lib"}" {corner}',
        f'.lib "{models / "cornerMOShv.lib"}" {corner}',
        f'.lib "{models / "cornerCAP.lib"}" cap_typ',
        f'.lib "{models / "cornerRES.lib"}" res_typ', f'.temp {temperature:g}',
        with_exposure_probe(translator, translator_name), replica,
        f'Xreplica VDD 0 VLOGIC BIAS {replica_name}',
        # The supply impedances/decoupling are the established loaded TX bench
        # conditions. They are explicit test conditions, not chip PDN proof.
        'VSUP SOURCE 0 3.3', 'VCORE CORE_SOURCE 0 1.2',
        'RSUP SOURCE VDD .25', 'RCORE CORE_SOURCE VLOGIC 2',
        'CSUP VDD 0 20p', 'CCORE VLOGIC 0 20p',
        'VCMD COMMAND 0 PWL(' + ' '.join(f'{t:.17g} {v}' for t, v in wave) + ')']
    observables = ['v(VDD)', 'v(VLOGIC)', 'v(BIAS)', 'v(COMMAND)']
    for index in range(22):
        lines += [f'Xgate{index} VDD 0 VLOGIC BIAS A{index} Y{index} YN{index} {translator_name}',
            f'Rinput{index} COMMAND A{index} 50', f'Cload{index} Y{index} 0 2p',
            f'Rload{index} Y{index} 0 10000']
        observables += [f'v(Y{index})', f'v(YN{index})', f'v(A{index})', f'v(Xgate{index}.bank_lv_exposure)']
    lines += ['.options klu method=gear maxord=2', '.save ' + ' '.join(observables),
        '.control', 'set num_threads=1', 'set wr_singlescale', 'set wr_vecnames', 'set numdgt=17',
        'tran 31.25p 2128n 0 31.25p uic', 'wrdata trace.dat ' + ' '.join(observables),
        'quit', '.endc', '.end']
    return re.sub(r'^(X[^\n]+\bcap_cmim\b[^\n]*)$', r'\1 ic=0', '\n'.join(lines) + '\n', flags=re.M)
