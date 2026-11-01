#!/usr/bin/env python3
"""Reject incomplete or altered electrical controller/oscillator bindings."""
import argparse
import json
from pathlib import Path
import subprocess
import tempfile

from processing_clock_closed import bind_live


def run(args):
    with tempfile.TemporaryDirectory(prefix='processing-clock-bindings-') as directory:
        work = Path(directory)
        subprocess.run([str(args.bench.resolve()), str(work), 'export'], check=True)
        model = (work / 'design.va').read_text()
        original = (work / 'bench.cir').read_text()
        metadata = json.loads((work / 'live.json').read_text())
        models = args.pdk.resolve() / 'ihp-sg13g2/libs.tech/ngspice/models'
        deck, binding = bind_live(model, original, models, metadata)
        if sum(binding['roles'].values()) != 51 or deck.count('\nBhost') != 34:
            raise ValueError('incomplete positive controller/oscillator binding')
        drivers = [line for line in model.splitlines() if '// sim_driver' in line]
        channels = [line for line in model.splitlines() if '<+ cpphdl_channel' in line]
        if len(drivers) != 34 or len(channels) != 51:
            raise ValueError('changed exported electrical inventory')
        damaged = {
            'missing_driver': model.replace(drivers[0] + '\n', '', 1),
            'duplicate_driver': model.replace(drivers[0], drivers[0] + '\n' + drivers[0], 1),
            'changed_impedance': model.replace(drivers[0], drivers[0].replace('/ 50.0;', '/ 5.0;'), 1),
            'unbound_control': model.replace(drivers[0], drivers[0].replace('cpphdl_bit0', 'cpphdl_bit99'), 1),
            'missing_transistor': model.replace(channels[0] + '\n', '', 1),
            'duplicate_transistor': model.replace(channels[0], channels[0] + '\n' + channels[0], 1),
        }
        for name, candidate in damaged.items():
            if candidate == model:
                raise ValueError('negative binding control did not change the design: ' + name)
            try:
                bind_live(candidate, original, models, metadata)
            except ValueError:
                continue
            raise ValueError('accepted damaged electrical binding: ' + name)
        print('PASS 51 physical MOS, 34 host branches and six rejected damaged bindings')


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--bench', type=Path, required=True)
    parser.add_argument('--pdk', type=Path, required=True)
    run(parser.parse_args())
