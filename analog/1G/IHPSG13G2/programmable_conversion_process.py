"""Bind programmable receiver conversions while preserving bench-only drivers."""
import re
from programmable_array_process import process_deck as bind_receiver


def process_deck(model, original, models, metadata, corner='mos_tt', temperature=27):
    cancellation = metadata.get('sampler_dummy_p_beta_A_per_V2', .005)
    if cancellation not in (.005, .006):
        raise ValueError('unsupported closed-conversion charge cancellation')
    count = metadata['slices']
    base_count = 7 + 2 * count
    observations = dict((int(i), n) for i, n in re.findall(r'^let observation(\d+) = v\((n\d+)\)$', original, re.M))
    if (set(observations) != set(range(base_count + count))
            or metadata['decisions'] != [observations[base_count + i] for i in range(count)]):
        raise ValueError('changed programmable conversion observation interface')
    observation_lines = [line for line in original.splitlines() if line.startswith('let observation')]
    writers = [line for line in original.splitlines() if line.startswith('wrdata ')]
    if len(writers) != 1 or writers[0] != 'wrdata spice.dat ' + ' '.join('observation' + str(i) for i in range(base_count + count)):
        raise ValueError('changed programmable conversion writer')
    lines = []
    for line in original.splitlines():
        match = re.fullmatch(r'let observation(\d+) = .*', line)
        if match and int(match[1]) >= base_count:
            continue
        if line.startswith('wrdata '):
            line = 'wrdata spice.dat ' + ' '.join('observation' + str(i) for i in range(base_count))
        lines.append(line)
    deck, inventory = bind_receiver(model, '\n'.join(lines) + '\n', models, corner, temperature,
                                    count=count, storage=metadata['sample_storage_F'], host_controls=metadata['drivers'],
                                    dummy_p_width_um=25 if cancellation == .005 else 30,
                                    charge_defined_nodes=metadata.get('charge_defined_nodes', False))
    outputs = []
    for part in inventory:
        if part['source'] == 'level_down.hpp' and part['model'] == 'sg13_lv_nmos':
            line = next(line for line in deck.splitlines() if line.startswith('Xphysical' + part['device'] + ' '))
            outputs.append(line.split()[1])
    if outputs != metadata['decisions']:
        raise ValueError('conversion feedback is not driven by the actual core interfaces')
    lines = []
    inserted = False
    for line in deck.splitlines():
        if line.startswith('let observation'):
            if not inserted:
                lines += observation_lines
                inserted = True
            continue
        if line.startswith('wrdata '):
            line = writers[0]
        lines.append(line)
    return '\n'.join(lines) + '\n', inventory
