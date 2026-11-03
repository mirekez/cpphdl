#!/usr/bin/env python3
"""Evaluate bounded receiver clock-drive changes with fresh RC and hold guards."""
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
    parser.add_argument('--trials', type=int, default=16)
    args = parser.parse_args()
    if not 1 <= args.trials <= 200:
        parser.error('clock trials must be in [1, 200]')
    run(args, HERE / 'receiver_skew_stage.tcl',
        [Path(__file__).resolve(), HERE / 'receiver_clock_path_trials.tcl',
         HERE / 'receiver_timing_tradeoffs.tcl'], dict(RECEIVER_SKEW_TRIALS=str(args.trials)))
