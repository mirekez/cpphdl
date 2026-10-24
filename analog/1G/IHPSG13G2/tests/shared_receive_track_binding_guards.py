"""Negative export guards for the independent shared TRACK process binding."""
from pathlib import Path
import json
import re
import sys

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))
from shared_receive_track_process import process_deck
from mos_geometry import size_mos_instances


def check(case):
    model, deck = (case / 'design.va').read_text(), (case / 'bench.cir').read_text()
    result, _ = process_deck(model, deck, Path('/models'))
    _, geometry = size_mos_instances(result)
    if len(geometry) != 655:
        raise ValueError('missing loaded TRACK devices')
    obs = dict(re.findall(r'^let observation(\d+) = v\((n\d+)\)$', deck, re.M))
    changes = {
        'request_strength': (model.replace('0.0080000000000000002', '0.009', 1), deck),
        'main_strength': (model.replace('0.029999999999999999', '0.031', 1), deck),
        'nonzero_initial_charge': (model, deck.replace('IC=0.0', 'IC=0.1', 1)),
        'sampler_clock_storage': (model, deck.replace(' 2e-14 IC', ' 3e-14 IC', 1)),
    }
    delay_cap = re.compile(r'^(Cdevice\d+ ' + obs['166'] + r' 0 )\S+( IC=\S+)$', re.M)
    changes['event_delay_storage'] = model, delay_cap.sub(r'\g<1>5e-13\2', deck)
    delay_law = re.compile(r'^\s*I\(n\d+, n\d+\) <\+ V\(n\d+, n\d+\) / 1000[.]0;$', re.M)
    changes['event_delay_resistor'] = delay_law.sub('', model), deck
    for label, index, other in [('strobe_bypass', 5, 4), ('event_bypass', 165, 5),
            ('missing_last_sampler', 93, 86), ('selection_alias', 90, 83),
            ('wrong_last_state', 91, 84), ('missing_core_probe', 164, 160),
            ('pulse_delay_probe', 166, 165), ('pulse_inversion_probe', 167, 168)]:
        changes[label] = model, deck.replace(f'let observation{index} = v({obs[str(index)]})',
                                            f'let observation{index} = v({obs[str(other)]})')
    rejected = []
    for label, pair in changes.items():
        if pair == (model, deck):
            raise ValueError('guard mutation did not apply: ' + label)
        try:
            process_deck(*pair, Path('/models'))
        except ValueError:
            rejected.append(label)
        else:
            raise ValueError('unsafe export accepted: ' + label)
    return dict(status='pass', physical_mos=len(geometry), rejected_mutations=rejected)


if __name__ == '__main__':
    print(json.dumps(check(Path(sys.argv[1])), indent=2))
