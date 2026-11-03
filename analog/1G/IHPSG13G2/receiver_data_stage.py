#!/usr/bin/env python3
"""Try data-path drive changes and retain only a checked physical improvement."""
import argparse
from pathlib import Path

from receiver_checkpoint_stage import run
from receiver_path_sizing import HERE, ROOT
from tool_process import configure_cleanup


if __name__ == '__main__':
    configure_cleanup()
    parser = argparse.ArgumentParser(description=__doc__)
    for name in ('origin', 'constraints', 'models', 'output'):
        parser.add_argument('--' + name, type=Path, required=True)
    parser.add_argument('--pdk', type=Path, default=ROOT / 'pdk/IHPSG13G2')
    parser.add_argument('--tools', type=Path, default=ROOT / 'out/repeater-toolchain')
    parser.add_argument('--trials', type=int, default=48)
    args = parser.parse_args()
    if not 1 <= args.trials <= 200:
        parser.error('data trials must be in [1, 200]')
    run(args, HERE / 'receiver_data_stage.tcl',
        [Path(__file__).resolve(), HERE / 'receiver_data_trials.py',
         HERE / 'receiver_path_sizing.py', HERE / 'receiver_timing_tradeoffs.tcl',
         HERE / 'receiver_checkpoint_choice.tcl'],
        dict(RECEIVER_DATA_TRIALS=str(args.trials), RECEIVER_MODELS=str(args.models.resolve()),
             RECEIVER_PYTHON=str(args.pdk.resolve() / '.cpphdl/venv/bin/python')))
