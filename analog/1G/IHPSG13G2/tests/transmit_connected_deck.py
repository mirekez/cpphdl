"""Loaded experiment for the physically realized C++ transmitter partition."""
import math
import re

from core_transmit_driver import HEADER, TRIM_HEADER


def solver_options(tight=False):
    # The R3 model can fail Newton iteration near zero terminal voltage with
    # 0.1 fA absolute current tolerance. Keep voltage accuracy explicit and
    # qualify 1 pA against a tenfold tighter current/charge profile instead.
    current, charge = ('1e-13', '1e-17') if tight else ('1e-12', '1e-16')
    return ('.options klu method=gear maxord=2 reltol=1e-5 vntol=1e-8 '
            f'abstol={current} chgtol={charge}')


def experiment(model, assembly, words, models, *, corner='mos_tt', temperature=27, trim=44):
    if (corner not in ('mos_tt', 'mos_ss', 'mos_ff') or not math.isfinite(temperature)
            or not -40 <= temperature <= 125 or not isinstance(trim, int) or not 0 <= trim < 64):
        raise ValueError('unsupported connected transmitter profile')
    banks = ('MAIN_P', 'MAIN_N', 'HISTORY_P', 'HISTORY_N')
    expected = ['VDD', 'VSS', 'VLOGIC', 'P', 'N']
    expected += [bank + str(bit) for bank in banks for bit in range(4)]
    expected += ['TRIM_N' + str(bit) for bit in range(6)]
    if assembly['ports'] != expected or len(words) < 100:
        raise ValueError('changed connected transmitter interface or incomplete stimulus')
    instances = {part['name']: part for part in assembly['instances']}
    if len(instances) != 45 or assembly['original_contributions'] != 931:
        raise ValueError('incomplete connected transmitter assembly')

    # Match core_transmit_fixture.hpp: finite supplies, source impedances,
    # cold storage, controller transitions and the same sampling apertures.
    ui, startup = 8e-9, 512e-9
    interface = dict(ui_s=ui, startup_s=startup, stop_s=startup + len(words) * ui,
        step_s=ui / 512, load_ohm=100, filtered=True, warm_words=34, trim_code=trim)
    lines = ['* Connected C++ transmitter: schematic or retained component RC',
        *[f'.lib "{models / library}" {selection}' for library, selection in (
            ('cornerMOSlv.lib', corner), ('cornerMOShv.lib', corner),
            ('cornerCAP.lib', 'cap_typ'), ('cornerRES.lib', 'res_typ'),
            ('cornerMOSCAP.lib', 'moscap_tt'))], f'.temp {temperature:g}', model,
        'Xdut ' + ' '.join('0' if port == 'VSS' else port for port in expected) + ' ConnectedTransmitter',
        'VSUP SOURCE 0 3.3', 'VCORE CORE_SOURCE 0 1.2',
        'RSUP SOURCE VDD .25', 'RCORE CORE_SOURCE VLOGIC 2',
        'CSUP VDD 0 20p IC=0', 'CCORE VLOGIC 0 20p IC=0', 'RLOAD P N 100']
    for bank in banks:
        for bit in range(4):
            port, previous = bank + str(bit), False
            wave = [(0., 0.)]
            for index, word in enumerate(words):
                value = bool(word[bank.lower()] & (1 << bit))
                if value != previous:
                    time = startup + (index + .15) * ui
                    wave += [(time, 1.2 if previous else 0), (time + .1e-9, 1.2 if value else 0)]
                    previous = value
            lines += [f'Vcmd_{port} cmd_{port} 0 PWL(' +
                      ' '.join(f'{t:.17g} {v:.17g}' for t, v in wave) + ')',
                      f'Rcmd_{port} cmd_{port} {port} 50']
    for bit in range(6):
        value = 0 if trim & (1 << bit) else 1.2
        lines += [f'Vtrim{bit} cmd_trim{bit} 0 {value:.17g}', f'Rtrim{bit} cmd_trim{bit} TRIM_N{bit} 50']

    def voltage(instance, port):
        node = instances[instance]['ports'][port]
        return 'v(' + (node if node in expected else 'Xdut.' + node) + ')'

    observables = [voltage('translator_' + str(index), 'Y') for index in range(16)]
    observables += ['v(P)', 'v(N)', 'v(P,N)', voltage('current_bias', 'BIAS'),
        'v(VDD)', 'v(SOURCE,VDD)', voltage('translation_bias', 'BIAS'), 'v(VLOGIC)',
        'v(CORE_SOURCE,VLOGIC)']
    observables += ['v(' + bank + str(bit) + ')' for bank in banks for bit in range(4)]
    for bit in range(6):
        observables += [f'v(TRIM_N{bit})', voltage('translator_' + str(16 + bit), 'Y')]
    observables += [voltage('current_bias', 'CASCODE')]
    # Keep the same 1.65 V LV gate-exposure bound used by the standalone bank,
    # now with the actual nonlinear steering and trim loads attached.
    observables += [f'v(Xdut.Xtranslator_{index}.bank_lv_exposure)' for index in range(22)]
    header = HEADER + TRIM_HEADER + ['CASCODE_BIAS'] + ['LV_EXPOSURE' + str(i) for i in range(22)]
    if len(observables) + 1 != len(header):
        raise ValueError('changed connected transmitter observations')
    # Save the underlying nodes as well: ngspice evaluates differential
    # vectors after the transient. Omitting either endpoint can lose them.
    saves = sorted({f'v({node})' for value in observables for node in value[2:-1].split(',')})
    lines += [solver_options(),
        '.save ' + ' '.join(saves), '.control', 'set num_threads=1',
        'set wr_singlescale', 'set wr_vecnames', 'set numdgt=17',
        f'tran {ui / 1024:.17g} {interface["stop_s"]:.17g} 0 {ui / 1024:.17g} uic',
        *[f'let observation{index} = {value}' for index, value in enumerate(observables)],
        'wrdata trace.dat ' + ' '.join('observation' + str(i) for i in range(len(observables))),
        'quit', '.endc', '.end']
    deck = '\n'.join(lines) + '\n'
    def cold_storage(match):
        line = match[0]
        existing = re.findall(r'\bic\s*=\s*(\S+)', line, re.I)
        if existing and (len(existing) != 1 or float(existing[0]) != 0):
            raise ValueError('nonzero or ambiguous initial MIM storage charge')
        return line if existing else line + ' ic=0'
    deck = re.sub(r'^X[^\n]+\bcap_cmim\b[^\n]*$', cold_storage, deck, flags=re.M)
    return deck, interface, header


def compare(first, second):
    """Keep the existing 10 mV loaded-TX plateau comparison allowance."""
    a, b = first['plateau_bounds_V'], second['plateau_bounds_V']
    limit = .010
    if (not a or len(a) != len(b) or any(len(row) != 6 for row in a + b)
            or any(len(pair) != 2 for row in a + b for pair in row)):
        raise ValueError('incomplete connected transmitter comparison')
    # Comparing both interval ends retains ambiguity at equal-time states.
    errors = [abs(x - y) for row_a, row_b in zip(a, b)
              for pair_a, pair_b in zip(row_a, row_b) for x, y in zip(pair_a, pair_b)]
    if not all(math.isfinite(v) for v in errors):
        raise ValueError('invalid connected transmitter comparison envelope')
    maximum = max(errors)
    if maximum > limit:
        raise ValueError(f'connected transmitter departure exceeds {limit:g} V: {maximum:.17g}')
    return dict(maximum_plateau_difference_V=maximum, limit_V=limit,
                checked_columns=['P', 'N', 'DIFF', 'BIAS', 'VDD', 'SUPPLY_DROP'])
