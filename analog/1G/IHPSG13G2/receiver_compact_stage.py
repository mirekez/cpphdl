#!/usr/bin/env python3
"""Remove short data-buffer chains from a completed receiver repair checkpoint."""
import argparse
import math
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
    parser.add_argument('--rounds', type=int, default=6)
    parser.add_argument('--span-um', type=float, default=120)
    args = parser.parse_args()
    if not 1 <= args.rounds <= 8 or not math.isfinite(args.span_um) or not 0 < args.span_um <= 200:
        parser.error('rounds must be in [1, 8], span in (0, 200] um')
    run(args, HERE / 'receiver_compact_stage.tcl',
        [Path(__file__).resolve(), HERE / 'receiver_local_unbuffer.tcl'],
        dict(RECEIVER_COMPACT_ROUNDS=str(args.rounds), RECEIVER_COMPACT_SPAN=str(args.span_um)))
