"""Check actual C++ electrical nodes and join them to converted digital ports.

The Verilog view is a connectivity/implementation blackbox, never a behavioral
replacement for the accompanying transistor-law Verilog-A circuit.
"""
from collections import Counter
import re


def expected_bindings():
    result = dict(REF_CLOCK='ref_clk', CLOCK_CLEAR='reset', ADC_CLOCK='adc_clk',
                  PROCESSING_RAW='raw_clk', PROCESSING_CLOCK='pcs_clk',
                  PROCESSING_CLEAR='processing_clear_out', PROCESSING_ADMIT='processing_admit_out')
    for bit in range(8):
        for kind in ('coarse', 'fine'):
            result[f'PROCESSING_{kind.upper()}_N_{bit}'] = f'processing_{kind}_n_out[{bit}]'
    for port in range(2):
        for name, signal in [('COMMAND', 'pulse_out'), ('ENABLE', 'pulse_enable_out'), ('DETECTED', 'pulse_in')]:
            result[f'PULSE_{name}_{port}'] = f'{signal}[{port}]'
    for lane in range(8):
        result[f'CLEAR_{lane}'] = f'clear_out[{lane}]'
        result[f'EVALUATE_ENABLE_{lane}'] = f'evaluate_enable_out[{lane}]'
        for bank in ('MAIN_P', 'MAIN_N', 'HISTORY_P', 'HISTORY_N'):
            for bit in range(4):
                result[f'{bank}_{lane}_{bit}'] = f'{bank.lower()}_out[{lane}][{bit}]'
        for s in range(13):
            result[f'TRACK_{lane}_{s}'] = f'acquire_out[{16 * lane + s}]'
            result[f'DECISION_{lane}_{s}'] = f'comparisons_in[{lane}][{s}]'
            for bit in range(8):
                result[f'TRIAL_{lane}_{s}_{bit}'] = f'trial_out[{16 * lane + s}][{bit}]'
    return result


def check(data):
    ports = {p['name']: p for p in data['ports']}
    if len(ports) != len(data['ports']):
        raise ValueError('duplicate electrical terminal name')
    expected = expected_bindings()
    external = {'VDD', 'VSS', 'VLOGIC'} | {
        f'{polarity}_{i}' for i in range(8) for polarity in ('P', 'N', 'SENSE_P', 'SENSE_N')}
    external |= {f'{name}_{lane}_{bit}' for name, width in [('GAIN_N', 5), ('TRIM_N', 6)]
                 for lane in range(8) for bit in range(width)}
    if set(ports) != set(expected) | external:
        raise ValueError('missing or extra electrical terminal')
    if len({p['node'] for p in ports.values()}) != len(ports):
        raise ValueError('distinct external terminals are shorted')
    for name, p in ports.items():
        if p['digital'] != expected.get(name, ''):
            raise ValueError('wrong digital bit: ' + name)
        direction = 'inout' if name in {'VDD', 'VSS', 'VLOGIC'} or re.fullmatch(r'[PN]_\d', name) else (
            'output' if name in {'ADC_CLOCK', 'PROCESSING_RAW', 'PROCESSING_CLOCK'} or
            name.startswith(('DECISION_', 'PULSE_DETECTED_')) else 'input')
        if p['direction'] != direction:
            raise ValueError('wrong terminal direction: ' + name)
    a = data['anchors']
    live = {node for element in data['elements'] for node in element['nodes']}
    if any(p['node'] not in live or a.get(p['name']) != p['node'] for p in ports.values()):
        raise ValueError('external terminal does not reach circuit laws')
    if any(n not in live for n in a.values()):
        raise ValueError('disconnected internal terminal')
    counts = Counter(e['kind'] for e in data['elements'])
    if set(counts) != {'resistor', 'capacitor', 'nmos', 'pmos'}:
        raise ValueError('missing physical laws or simulation-only device')

    def same(*names):
        try:
            if len({a[name] for name in names}) != 1:
                raise ValueError('disconnected terminals: ' + ', '.join(names))
        except KeyError as error:
            raise ValueError('missing circuit terminal: ' + str(error)) from error

    def supplies(prefix, core=True):
        same('VDD', prefix + '.Vdd')
        same('VSS', prefix + '.Vss')
        if core:
            same('VLOGIC', prefix + '.Vlogic')

    same('VLOGIC', 'processing.Vdd', 'adc_clock.Vlogic')
    same('VSS', 'processing.Vss')
    for name, anchor in [('REF_CLOCK', 'adc_clock.REF'), ('CLOCK_CLEAR', 'adc_clock.CLEAR'),
                         ('ADC_CLOCK', 'adc_clock.CORE_OUT'), ('PROCESSING_RAW', 'processing.RAW'),
                         ('PROCESSING_CLOCK', 'processing.OUT'), ('PROCESSING_CLEAR', 'processing.CLEAR'),
                         ('PROCESSING_ADMIT', 'processing.ADMIT')]:
        same(name, anchor)
    for bit in range(8):
        for name in ('COARSE_N', 'FINE_N'):
            same(f'PROCESSING_{name}_{bit}', f'processing.{name}[{bit}]')
    for p in range(2):
        supplies(f'common{p}', False)
        supplies(f'range{p}', False)
        supplies(f'flp{p}')
        for name in ('COMMAND', 'ENABLE'):
            same(f'PULSE_{name}_{p}', f'flp{p}.{name}')
        same(f'PULSE_DETECTED_{p}', f'flp{p}.PULSE')
        for polarity in ('P', 'N'):
            same(f'{polarity}_{4 * p}', f'flp{p}.TX_{polarity}')
            same(f'SENSE_{polarity}_{4 * p + 1}', f'flp{p}.RX_{polarity}')
    held = []
    for lane in range(8):
        r, t = f'rx{lane}', f'tx{lane}'
        supplies(r)
        supplies(t)
        for polarity in ('P', 'N'):
            same(f'{polarity}_{lane}', f'{t}.{polarity}', f'{t}.output.{polarity}')
            same(f'SENSE_{polarity}_{lane}', f'{r}.{polarity}', f'{r}.input.{polarity}')
        same(f'common{lane // 4}.OUT', r + '.VCM', r + '.input.VCM')
        same(f'range{lane // 4}.OUT', r + '.VREF', r + '.preamp.VREF')
        for polarity in ('PLUS', 'MINUS'):
            same(f'{r}.input.{polarity}', f'{r}.preamp.{polarity}')
        same('ADC_CLOCK', r + '.ADC_CLOCK', r + '.evaluation.CLOCK')
        same('VLOGIC', r + '.evaluation.Vdd')
        same(f'EVALUATE_ENABLE_{lane}', r + '.EVALUATE_ENABLE', r + '.evaluation.ENABLE')
        same(f'CLEAR_{lane}', r + '.CLEAR')
        for bit in range(5):
            same(f'GAIN_N_{lane}_{bit}', f'{r}.gain.A[{bit}]')
            same(f'{r}.gain.Y[{bit}]', f'{r}.preamp.SELECT_N[{bit}]')
        for bit in range(6):
            same(f'TRIM_N_{lane}_{bit}', f'{t}.trim.A[{bit}]')
        for bank, name in enumerate(('MAIN_P', 'MAIN_N', 'HISTORY_P', 'HISTORY_N')):
            for bit in range(4):
                same(f'{name}_{lane}_{bit}', f'{t}.gate.A[{4 * bank + bit}]')
        for s in range(13):
            path = f'{r}.s{s}'
            supplies(path)
            same(r + '.preamp.OUT', path + '.IN', path + '.sampler.IN')
            same(f'TRACK_{lane}_{s}', path + '.tracking.A')
            same(path + '.tracking.Y', path + '.sampler.CLOCK')
            same(path + '.sampler.OUT', path + '.decision.IN')
            same(path + '.decision.BIAS', path + '.tracking.BIAS')
            same(f'CLEAR_{lane}', path + '.CLEAR', path + '.decision.CLEAR')
            same(r + '.evaluation.OUT', path + '.EVALUATE', path + '.decision.EVALUATE')
            same(f'DECISION_{lane}_{s}', path + '.decision.CORE_DECISION')
            for bit in range(8):
                same(f'TRIAL_{lane}_{s}_{bit}', f'{path}.decision.TRIAL[{bit}]', f'{path}.dac.A[{bit}]')
            held.append(a[path + '.sampler.OUT'])
    if len(set(held)) != 104 or set(held) & {p['node'] for p in ports.values()}:
        raise ValueError('hold capacitors aliased to another slice or boundary')
    # Each held voltage is actual storage, with a real sampling switch and
    # decision input. Merely naming a disconnected pin cannot satisfy this.
    capacitors = {e['nodes'][0] for e in data['elements'] if e['kind'] == 'capacitor' and e['nodes'][1] == a['VSS']}
    if not set(held) <= capacitors:
        raise ValueError('missing held-node storage')
    return dict(receive_lanes=8, transmit_lanes=8, sar_slices=104, flp_interfaces=2,
                digital_bit_connections=len(expected), electrical_terminals=len(ports),
                circuit_laws=dict(counts), full_chip_qualified=False)


def rtl_ports(source):
    match = re.search(r'module\s+RepeaterDigital\s*\((.*?)\);', source, re.S)
    if not match:
        raise ValueError('missing converted digital top')
    ports = {}
    for declaration in match[1].split(','):
        m = re.fullmatch(r'\s*(input|output)\s+wire\s*(\[\d+-1:0\])?\s*(\w+)\s*(\[\d+\])?\s*', declaration)
        if not m:
            raise ValueError('unsupported converted port: ' + declaration)
        direction, packed, name, array = m.groups()
        if name in ports:
            raise ValueError('duplicate RTL port')
        ports[name] = dict(direction=direction, packed=packed or '', array=array or '',
                           width=int(packed[1:].split('-')[0]) if packed else 1,
                           length=int(array[1:-1]) if array else None)
    return ports


def join(data, source):
    check(data)
    ports = rtl_ports(source)
    internal = set()
    for p in data['ports']:
        if not p['digital']:
            continue
        expr = p['digital']
        name = expr.split('[')[0]
        if name not in ports:
            raise ValueError('missing digital port: ' + name)
        v = ports[name]
        subscripts = [int(i) for i in re.findall(r'\[(\d+)\]', expr)]
        limits = ([v['length']] if v['length'] is not None else []) + ([v['width']] if v['packed'] else [])
        if len(subscripts) != len(limits) or any(i >= limit for i, limit in zip(subscripts, limits)):
            raise ValueError('non-scalar or out-of-range connection: ' + expr)
        external_drive = name in {'ref_clk', 'reset'}
        required_direction = 'input' if p['direction'] == 'output' or external_drive else 'output'
        if v['direction'] != required_direction:
            raise ValueError('driver collision: ' + expr)
        if not external_drive:
            internal.add(name)
    def decl(name, p):
        return f"wire{p['packed']} {name}{p['array']}"
    boundary = [f"{p['direction']} {decl(name, p)}" for name, p in ports.items() if name not in internal]
    boundary += [f"{p['direction']} wire {p['name']}" for p in data['ports'] if not p['digital']]
    lines = ['`default_nettype none', '// Electrical connectivity only; analog implementation is in RepeaterAnalog.va.',
             'module RepeaterElectrical(\n    ' + ',\n    '.join(boundary) + '\n);']
    lines += ['    ' + decl(name, ports[name]) + ';' for name in sorted(internal)]
    # Digital reserved slots are explicit zeros, never fabricated comparators.
    lines += [f"    assign comparisons_in[{lane}][15:13] = 3'b000;" for lane in range(8)]
    lines += ['    RepeaterDigital digital(\n        ' + ',\n        '.join(f'.{name}({name})' for name in ports) + '\n    );']
    lines += ['    RepeaterAnalog electrical(\n        ' + ',\n        '.join(
        f".{p['name']}({p['digital'] or p['name']})" for p in data['ports']) + '\n    );', 'endmodule']
    abstract = ['`default_nettype none', '// Implementation blackbox, not a simulation model.',
                '(* blackbox *) module RepeaterAnalog(\n    ' + ',\n    '.join(
                    f"{p['direction']} wire {p['name']}" for p in data['ports']) + '\n);', 'endmodule']
    return '\n'.join(lines) + '\n', '\n'.join(abstract) + '\n'
