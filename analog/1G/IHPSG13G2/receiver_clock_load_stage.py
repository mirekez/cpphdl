#!/usr/bin/env python3
"""Repair explicit overloaded receiver clock branches on a retained checkpoint."""
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
    args = parser.parse_args()
    run(args, HERE / 'receiver_clock_load_stage.tcl',
        [Path(__file__).resolve(), HERE / 'receiver_clock_loads.tcl'], {})
