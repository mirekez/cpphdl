"""Reuse independent cable/frame stimuli with no per-symbol training answers."""
import json
from receive_dsp_pcs_reference import generate as generate_channel


def generate(directory):
    summaries = generate_channel(directory, quiet_symbols=32768)
    for path in directory.glob('case*.txt'):
        rows = path.read_text().splitlines()
        count = int(rows[0].split()[1])
        # The DUT fixture receives only achieved ADC observations and local
        # transmit references. Frame truth remains separate for output scoring.
        rows[1:count + 1] = [' '.join(row.split()[:2]) for row in rows[1:count + 1]]
        path.write_text('\n'.join(rows) + '\n')
    for item in summaries:
        item.pop('train_symbols')
        item['supplied_training_targets'] = False
    (directory / 'cases.json').write_text(json.dumps(summaries, indent=2) + '\n')
    return summaries
