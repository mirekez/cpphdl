#!/usr/bin/env python3
"""Reject altered receive DAC inventory, polarity, cold charge and connectivity."""
import argparse
from pathlib import Path
import re
import subprocess
import sys
import tempfile

SOURCE = Path(__file__).resolve()
sys.path.insert(0, str(SOURCE.parents[1]))
from core_receive_dac_process import process_deck
from mos_geometry import size_mos_instances


def run(args):
    with tempfile.TemporaryDirectory(prefix='core-receive-dac-binding-') as directory:
        work = Path(directory)
        subprocess.run([str(args.bench.resolve()), str(work), 'export'], check=True, timeout=60)
        model, deck = (work / 'design.va').read_text(), (work / 'bench.cir').read_text()
        models = args.pdk.resolve() / 'ihp-sg13g2/libs.tech/ngspice/models'
        mapped, inventory = process_deck(model, deck, models)
        sized, geometry = size_mos_instances(mapped)
        assert sum(inventory['roles'].values()) == 113
        assert sum(d['kind'] == 'R' for d in inventory['devices']) == 29
        assert 'CircuitModel' not in sized and geometry
        observations = dict(re.findall(r'^let observation(\d+) = v\((n\d+)\)$', deck, re.M))
        caps = [line for line in deck.splitlines() if line.startswith('Cdevice')]
        bias_cap = next(line for line in caps if float(line.split()[3]) == 25e-12)
        rejected = []

        def reject(name, changed_model, changed_deck):
            assert (changed_model, changed_deck) != (model, deck), name
            try:
                process_deck(changed_model, changed_deck, models)
            except ValueError:
                rejected.append(name)
            else:
                raise AssertionError('invalid receive DAC binding accepted: ' + name)

        reject('unknown channel', model.replace('cpphdl_channel(', 'unknown_channel(', 1), deck)
        reject('aliased last core input', model, deck.replace('let observation39 = v(' + observations['39'] + ')',
            'let observation39 = v(' + observations['4'] + ')'))
        reject('aliased last high-voltage bit', model, deck.replace('let observation40 = v(' + observations['40'] + ')',
            'let observation40 = v(' + observations['5'] + ')'))
        reject('disconnected first ladder bit', re.sub(r'\b' + observations['5'] + r'\b', observations['10'], model), deck)
        reject('wrong core exposure probe', model, deck.replace('let observation43 = v(' + observations['43'] + ')',
            'let observation43 = v(' + observations['42'] + ')'))
        reject('wrong shared bias node', model, deck.replace('let observation3 = v(' + observations['3'] + ')',
            'let observation3 = v(' + observations['2'] + ')'))
        reject('missing storage', model, deck.replace(caps[-1] + '\n', ''))
        reject('nonzero startup charge', model, deck.replace(bias_cap, bias_cap.replace('IC=0.0', 'IC=0.01')))
        fields = bias_cap.split()
        fields[3] = '1e-12'
        reject('wrong shared bias storage', model, deck.replace(bias_cap, ' '.join(fields)))
        reject('wrong ladder resistance', model.replace('/ 750.0;', '/ 1500.0;', 1), deck)
        print('PASS 113-MOS receive DAC binding and', len(rejected), 'negative mutations')


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--bench', type=Path, required=True)
    parser.add_argument('--pdk', type=Path, required=True)
    run(parser.parse_args())
