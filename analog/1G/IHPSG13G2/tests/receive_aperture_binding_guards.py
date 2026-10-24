"""Negative tests for the independently bound edge-sensor candidate."""
import json
from pathlib import Path
import re
import sys

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))
from receive_aperture_sensor_process import process_deck


def run(export):
    model = (export/'design.va').read_text()
    deck = (export/'bench.cir').read_text()
    models = Path(__file__).resolve().parents[3]/'pdk/IHPSG13G2/ihp-sg13g2/libs.tech/ngspice/models'
    process_deck(model, deck, models)
    variants = {
        'logic_strength': (model.replace(', 0.01,', ', 0.011,', 1), deck),
        'logic_threshold': (model.replace('0.34999999999999998', '0.4', 1), deck),
        'attenuator_resistance': (model.replace('/ 7000.0;', '/ 7100.0;', 1), deck),
        'reference_arm_resistance': (model.replace('/ 2545.4545454545455;', '/ 2500.0;', 1), deck),
        'missing_channel': (re.sub(r'^.*cpphdl_channel\(V\(.*\n', '', model, count=1, flags=re.M), deck),
        'cold_storage': (model, re.sub(r'(Cdevice\d+ [^\n]+ IC=)0\b', r'\g<1>0.1', deck, count=1)),
        'missing_storage': (model, re.sub(r'^Cdevice[^\n]+\n', '', deck, count=1, flags=re.M)),
        'observation_wiring': (model, re.sub(r'(let observation5 = v\()n\d+', r'\g<1>n0', deck, count=1)),
    }
    result = {}
    for name, (m, d) in variants.items():
        if (m,d) == (model,deck):
            raise AssertionError('mutation did not apply: '+name)
        try:
            process_deck(m,d,models)
        except ValueError:
            result[name] = True
        else:
            raise AssertionError('accepted '+name)
    for name, corner, temperature in [('bad_corner','mos_unknown',27),('nonfinite_temperature','mos_tt',float('nan'))]:
        try:
            process_deck(model,deck,models,corner,temperature)
        except ValueError:
            result[name] = True
        else:
            raise AssertionError('accepted '+name)
    return dict(status='pass', rejected=result)


if __name__ == '__main__':
    print(json.dumps(run(Path(sys.argv[1])), indent=2))
