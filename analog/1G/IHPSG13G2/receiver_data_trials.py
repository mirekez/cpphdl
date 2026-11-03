"""Emit bounded, same-function up/down sizing trials for a live path report."""
import argparse
from pathlib import Path
import re

from receiver_path_sizing import data_trials
from buffer_equivalence import parse
from sizing_equivalence import models


def emit(paths, netlist, library, output):
    trials = data_trials(paths.read_text(), parse(netlist)['cells'], models(library))
    if any(not re.fullmatch(r'\w+', item) for trial in trials for item in trial):
        raise ValueError('unsafe sizing trial identifier')
    output.write_text('set data_trials {' + ' '.join('{' + ' '.join(trial) + '}'
                                                   for trial in trials) + '}\n')


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    for name in ('paths', 'netlist', 'models', 'output'):
        parser.add_argument('--' + name, type=Path, required=True)
    args = parser.parse_args()
    emit(args.paths, args.netlist, args.models, args.output)
