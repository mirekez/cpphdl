#!/usr/bin/env python3
"""Check the assembled MDI process binding and reject electrical mutations."""
import argparse
from pathlib import Path
import re
import subprocess
import sys
import tempfile

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))
from differential_preamplifier_process import process_deck
from mos_geometry import size_mos_instances


def run(args):
    with tempfile.TemporaryDirectory(prefix='mdi-preamplifier-bindings-') as temporary:
        directory = Path(temporary)
        subprocess.run([str(args.bench.resolve()), str(directory), 'export', '8', '5'], check=True)
        model = (directory / 'design.va').read_text()
        deck = (directory / 'bench.cir').read_text()

        def bind(m=model, d=deck):
            return process_deck(m, d, Path('/reviewed-models'))

        physical, inventory = bind()
        sized, geometry = size_mos_instances(physical)
        assert (inventory['mos_count'], inventory['resistor_count'], inventory['capacitor_count']) == (50, 55, 62)
        assert 'sg13_hv_nmos' in sized and 'sg13_hv_pmos' in sized and 'cpphdl_channel' not in sized
        changed_cap = re.sub(r'(?m)^(Cdevice\d+ \S+ \S+) \S+( IC=)', r'\1 1.234e-12\2', deck, count=1)
        charged_cap = re.sub(r'(?m)^(Cdevice\d+ [^\n]+ IC=)0[.]0$', r'\g<1>0.025', deck, count=1)
        missing_cap = re.sub(r'(?m)^Cdevice\d+ [^\n]+\n', '', deck, count=1)
        missing_law = re.sub(r'(?m)^.*I\(n\d+, n\d+\) <\+ cpphdl_channel[^\n]+\n', '', model, count=1)
        wrong_resistor = re.sub(r' / 100000[.]0;', ' / 100001.0;', model, count=1)
        unknown_beta = re.sub(r', 0[.]014999999999999999, 0[.]69999999999999996, 1[.]0\);', ', 0.016, 0.69999999999999996, 1.0);', model, count=1)
        # Match the actual textual beta representation without changing a
        # capacitor or inventing a second device stamp.
        if unknown_beta == model:
            unknown_beta = re.sub(r'(cpphdl_channel\([^\n]+, )0[.]014999999999999999(, )', r'\g<1>0.016\2', model, count=1)
        if unknown_beta == model:
            unknown_beta = re.sub(r'(cpphdl_channel\([^\n]+, )0[.]015(, )', r'\g<1>0.016\2', model, count=1)
        wrong_supply = re.sub(r'(?m)^let observation5 = v\(n\d+\)$', 'let observation5 = v(n0)', deck)
        variants = [(model, changed_cap), (model, charged_cap), (model, missing_cap),
                    (missing_law, deck), (wrong_resistor, deck), (unknown_beta, deck), (model, wrong_supply)]
        for changed_model, changed_deck in variants:
            if (changed_model, changed_deck) == (model, deck):
                raise ValueError('negative control did not change the actual export')
            try:
                bind(changed_model, changed_deck)
            except ValueError:
                continue
            raise ValueError('mutated preamplifier process export was accepted')
        print('PASS complete 50-MOS MDI preamplifier binding and seven negative controls')


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--bench', type=Path, required=True)
    run(parser.parse_args())
