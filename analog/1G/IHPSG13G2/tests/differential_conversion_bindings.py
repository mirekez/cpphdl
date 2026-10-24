#!/usr/bin/env python3
"""Bind real cold receiver exports and reject broken storage/control topology."""
import argparse
from collections import Counter
import copy
import json
from pathlib import Path
import re
import subprocess
import sys
import tempfile

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))
from differential_conversion_process import process_deck
from mos_geometry import size_mos_instances


def run(args):
    negatives = 0
    with tempfile.TemporaryDirectory(prefix='cold-conversion-bindings-') as temporary:
        for count in (1, 4, 13):
            directory = Path(temporary) / str(count)
            subprocess.run([str(args.bench.resolve()), str(directory), 'export', str(count), '4', '1.5',
                '2' if count == 13 else '1'], check=True)
            model = (directory / 'design.va').read_text()
            deck = (directory / 'bench.cir').read_text()
            metadata = json.loads((directory / 'live.json').read_text())

            def bind(m=model, d=deck, profile=metadata):
                return process_deck(m, d, Path('/reviewed-models'), profile)

            physical, inventory = bind()
            sized, geometry = size_mos_instances(physical)
            if len(inventory) != 50 + 59 * count or 'cpphdl_channel' in sized or 'sg13_hv_pmos' not in sized or 'sg13_lv_nmos' not in sized:
                raise ValueError('changed cold receiver inventory or unbound MOS law')
            conditions = json.loads((directory / 'conditions.json').read_text())
            if conditions['cpp_step_s'] != 2e-12 or conditions['driver_edge_s'] != 2e-15:
                raise ValueError('changed native grid or independent control edge')
            preamps = [part for part in inventory if part['source'] == 'nmos_resistor_amplifier.hpp']
            if Counter(part['width_um'] for part in preamps) != Counter({50: 2, 25: 2 * count}):
                raise ValueError('changed input/conversion preamplifier geometry')
            samplers = [part for part in inventory if part['source'] == 'mos_track_hold.hpp']
            if Counter(part['width_um'] for part in samplers) != Counter({37.5: count, 75: count, 20: count, 42: count}):
                raise ValueError('changed independently balanced main/dummy geometry')
            if (metadata.get('sampler_dummy_n_beta_A_per_V2') != .016 or
                    metadata.get('sampler_dummy_p_beta_A_per_V2') != .0084):
                raise ValueError('missing or changed independent dummy metadata')
            for part in inventory:
                if part['source'] not in ('mos_track_hold.hpp', 'nmos_resistor_amplifier.hpp'):
                    continue
                layout = next(g for g in geometry if g['instance'] == 'Xphysical' + part['device'])
                expected = ({37.5: 5, 75: 10, 20: 2, 42: 5}[part['width_um']]
                    if part['source'] == 'mos_track_hold.hpp' else {25: 5}.get(part['width_um']))
                if expected is not None and layout['fingers'] != expected:
                    raise ValueError('changed characterized sampling/preamp finger count')
            if count != 1:
                continue
            variants = []
            owner = next(part['instance'] for part in preamps if part['width_um'] == 25)
            for value, replacement in ((1000, 500), (100, 50), (300, 150)):
                pattern = (r'(?m)(// device \d+, IC ' + re.escape(owner) +
                    r', [^\n]+nmos_resistor_amplifier.hpp:\d+\n\s*I\([^\n]+ / )' +
                    re.escape(str(float(value))) + ';')
                changed, replacements = re.subn(pattern, lambda m: m[1] + str(float(replacement)) + ';', model, count=1)
                if replacements != 1:
                    raise ValueError('missing conversion preamplifier resistor negative control')
                variants.append(('conversion resistor ' + str(value), changed, deck, metadata))
            for key, value in (('cold_start', False), ('normalizer_offset', 128), ('controller_bank_slices', 16),
                    ('sampler_dummy_n_beta_A_per_V2', .01),
                    ('sampler_dummy_p_beta_A_per_V2', .004), ('sample_storage_F', 2e-12)):
                profile = copy.deepcopy(metadata)
                profile[key] = value
                variants.append((key, model, deck, profile))
            profile = copy.deepcopy(metadata)
            del profile['sampler_dummy_n_beta_A_per_V2']
            variants.append(('missing NMOS dummy metadata', model, deck, profile))
            for width, old_beta in ((37.5, .026), (75, .013), (20, .01), (42, .004)):
                part = next(part for part in samplers if part['width_um'] == width)
                pattern = (r'(?m)(// device ' + re.escape(part['device']) +
                    r', IC [^\n]+\n\s*I\([^\n]+cpphdl_channel\([^\n]+, )'
                    r'([-+0-9.eE]+)(, [-+0-9.eE]+, [-+0-9.eE]+\);)')
                changed, replacements = re.subn(pattern, lambda m: m[1] + str(old_beta) + m[3], model, count=1)
                if replacements != 1:
                    raise ValueError('missing independent sampler strength negative control')
                variants.append(('old sampler strength ' + str(width), changed, deck, metadata))
            part = next(part for part in samplers if part['width_um'] == 20)
            pattern = (r'(?m)(// device ' + re.escape(part['device']) +
                r', IC [^\n]+\n\s*I\([^\n]+cpphdl_channel\(V\()n\d+(,)')
            changed, replacements = re.subn(pattern, lambda m: m[1] + 'n0' + m[2], model, count=1)
            if replacements != 1:
                raise ValueError('missing independent dummy gate negative control')
            variants.append(('grounded NMOS dummy gate', changed, deck, metadata))
            profile = copy.deepcopy(metadata)
            profile['drivers'][1]['id'] = profile['drivers'][0]['id']
            variants.append(('aliased control IDs', model, deck, profile))
            profile = copy.deepcopy(metadata)
            profile['drivers'][0], profile['drivers'][1] = profile['drivers'][1], profile['drivers'][0]
            variants.append(('control ordering', model, deck, profile))
            profile = copy.deepcopy(metadata)
            profile['decisions'] = ['n0']
            variants.append(('decision observation', model, deck, profile))
            variants += [
                ('initial charge', model, re.sub(r'(?m)^(Cdevice\d+ [^\n]+ IC=)0[.]0$', r'\g<1>0.025', deck, count=1), metadata),
                ('missing storage', model, re.sub(r'(?m)^Cdevice\d+ [^\n]+\n', '', deck, count=1), metadata),
                ('missing MOS law', re.sub(r'(?m)^.*I\(n\d+, n\d+\) <\+ cpphdl_channel[^\n]+\n', '', model, count=1), deck, metadata),
                ('unreviewed resistance', re.sub(r' / 1275[.]0;', ' / 1276.0;', model, count=1), deck, metadata),
                ('wrong supply node', model, re.sub(r'(?m)^let observation4 = v\(n\d+\)$', 'let observation4 = v(n0)', deck), metadata),
            ]
            for name, changed_model, changed_deck, profile in variants:
                if (changed_model, changed_deck, profile) == (model, deck, metadata):
                    raise ValueError('negative control made no change: ' + name)
                try:
                    bind(changed_model, changed_deck, profile)
                except (ValueError, KeyError):
                    negatives += 1
                    continue
                raise ValueError('broken cold receiver binding accepted: ' + name)
    print('PASS real cold-start exports at 1/4/13 slices and', negatives, 'negative storage/control/topology checks')


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--bench', type=Path, required=True)
    run(parser.parse_args())
