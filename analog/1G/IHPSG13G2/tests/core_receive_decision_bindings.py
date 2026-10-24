#!/usr/bin/env python3
"""Reject disconnected/altered physical trial-to-decision experiments."""
import argparse
from pathlib import Path
import re
import subprocess
import sys
import tempfile

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))
from core_receive_decision_process import process_deck
from mos_geometry import size_mos_instances


def check(bench, pdk):
    with tempfile.TemporaryDirectory() as directory:
        work = Path(directory)
        subprocess.run([str(bench.resolve()), str(work), 'export'], check=True)
        model, original = (work / 'design.va').read_text(), (work / 'bench.cir').read_text()
        models = pdk.resolve() / 'ihp-sg13g2/libs.tech/ngspice/models'
        deck, inventory = process_deck(model, original, models)
        size_mos_instances(deck)
        assert len(inventory['devices']) == 174 and inventory['capacitors'] == 303
        obs = dict(re.findall(r'^let observation(\d+) = v\((n\d+)\)$', original, re.M))
        def probe(index, replacement):
            return original.replace(f'let observation{index} = v({obs[str(index)]})',
                f'let observation{index} = v({obs[str(replacement)]})')
        changes = [(model, probe(i, j)) for i, j in ((0, 1), (1, 0), (2, 3), (5, 4),
            (9, 8), (12, 11), (15, 9), (16, 20), (49, 48), (55, 54))]
        changes += [(model, original.replace('IC=0', 'IC=0.1', 1)),
            (model, re.sub(r'^Cdevice\d+ .*\n', '', original, count=1, flags=re.M)),
            (model.replace('/ 300.0;', '/ 301.0;', 1), original),
            (model.replace('0.0074999999999999997', '0.008', 1), original)]
        for i, (changed_model, changed_deck) in enumerate(changes):
            if (changed_model, changed_deck) == (model, original):
                raise AssertionError(f'mutation {i} did not alter the experiment')
            try:
                process_deck(changed_model, changed_deck, models)
            except ValueError:
                continue
            raise AssertionError(f'mutation {i} accepted')
        print('PASS physical connected decision binding and', len(changes), 'negative mutations')


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--bench', type=Path, required=True)
    parser.add_argument('--pdk', type=Path, required=True)
    args = parser.parse_args()
    check(args.bench, args.pdk)
