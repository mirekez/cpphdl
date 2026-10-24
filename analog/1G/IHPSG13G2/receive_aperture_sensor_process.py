"""Strict geometry binding for the initial open-loop aperture sensor only.

The canonical exported topology is deliberately locked. A topology or explicit
storage change needs a reviewed new binding revision, never inferred sizing.
"""
from collections import Counter
import hashlib
import math
import re
from core_driver_process import contributions

MODEL_SHA256 = 'afe6be13816dcecaa153e7a9eec94da4ca67d594a68009f612b5777a83e8aa1e'
PASSIVES_OBS_SHA256 = 'c6771dd2ca887cc0fede9893017315c972a6352127dfe2f9deef8cf0b77d527f'
TABLE = (
    ('logic_n', .01, .35, 1, 'sg13_lv_nmos', 10, .13, 131),
    ('logic_p', .01, .35, -1, 'sg13_lv_pmos', 20, .13, 131),
    ('clear_n', .05, .35, 1, 'sg13_lv_nmos', 50, .13, 8),
    ('clear_p', .05, .35, -1, 'sg13_lv_pmos', 100, .13, 8),
)


def process_deck(model, original, models, corner='mos_tt', temperature=27):
    if corner not in ('mos_tt', 'mos_ss', 'mos_ff', 'mos_sf', 'mos_fs'):
        raise ValueError('unknown sensor process corner')
    if not math.isfinite(temperature) or not -40 <= temperature <= 125:
        raise ValueError('invalid sensor temperature')
    if hashlib.sha256(model.encode()).hexdigest() != MODEL_SHA256:
        raise ValueError('changed sensor transistor/resistor topology or model')
    lines = original.splitlines()
    fixed = [x for x in lines if x.startswith('Cdevice') or re.match(r'let observation\d+ = ', x)]
    if hashlib.sha256(('\n'.join(fixed)+'\n').encode()).hexdigest() != PASSIVES_OBS_SHA256:
        raise ValueError('changed sensor storage, initial charge or observation wiring')
    devices, roles = contributions(model, TABLE)
    if roles != Counter({t[0]: t[-1] for t in TABLE}):
        raise ValueError('changed sensor MOS inventory')
    obs = dict(re.findall(r'^let observation(\d+) = v\((n\d+)\)$', original, re.M))
    core = obs['0']
    if lines.count('.control') != 1 or sum(x.startswith('Ndesign ') for x in lines) != 1:
        raise ValueError('changed sensor simulation interface')
    deck = [lines[0], f'.lib "{models / "cornerMOSlv.lib"}" {corner}', f'.temp {temperature}']
    for line in lines[1:]:
        if line.startswith(('.model CircuitModel ', 'Ndesign ', 'pre_osdi ')):
            continue
        if line == '.control':
            for i, d in enumerate(devices):
                if d['kind'] == 'R':
                    deck.append(f'Rphysical{i} {d["a"]} {d["b"]} {d["value"]:.17g}')
                else:
                    d['body'] = core if d['model'].endswith('pmos') else '0'
                    d['fingers'] = math.ceil(d['width_um']/10)
                    deck.append(f'Xphysical{i} {d["a"]} {d["gate"]} {d["b"]} {d["body"]} '
                                f'{d["model"]} w={d["width_um"]}u l={d["length_um"]}u ng={d["fingers"]}')
        deck.append(line)
    return '\n'.join(deck)+'\n', dict(devices=devices, roles=dict(roles),
        model_sha256=MODEL_SHA256, passives_observations_sha256=PASSIVES_OBS_SHA256,
        scope='initial dual-arm receiver/retimer process candidate, no closed-loop or sampler qualification')
