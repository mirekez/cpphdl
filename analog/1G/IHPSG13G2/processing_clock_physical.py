#!/usr/bin/env python3
"""Route and independently qualify the two-domain clock-controller macro."""
import argparse
from pathlib import Path
from types import SimpleNamespace
import tempfile

from prepare_processing_clock_physical import prepare
from physical_module import implement, configure_cleanup, ANALOG

SOURCE = Path(__file__).resolve()


def run(args):
    args.work_root.mkdir(parents=True, exist_ok=True)
    work = Path(tempfile.mkdtemp(prefix='ProcessingClockControl-', dir=args.work_root.resolve()))
    print('Clock-controller work: ' + str(work), flush=True)
    prepare(args.native_proof, args.cell_proof, work / 'inputs', extra_sources=[SOURCE])
    physical = SimpleNamespace(module='ProcessingClockControl', work=work / 'physical',
        mapped_work=work / 'inputs', pdk=args.pdk, tools=args.tools,
        verilator=str(args.verilator.resolve()), cxx=args.cxx, bounded_clock_run=args.bounded)
    return implement(physical)


if __name__ == '__main__':
    configure_cleanup()
    parser = argparse.ArgumentParser(description=__doc__)
    for name in ('native-proof', 'cell-proof', 'work-root', 'verilator'):
        parser.add_argument('--' + name, type=Path, required=True)
    parser.add_argument('--pdk', type=Path, default=ANALOG / 'pdk/IHPSG13G2')
    parser.add_argument('--tools', type=Path, default=ANALOG / 'out/repeater-toolchain')
    parser.add_argument('--bounded', action='store_true', help='use a hard 1 GiB per-process cap for concurrent execution')
    parser.add_argument('--cxx', default='/usr/bin/c++')
    raise SystemExit(run(parser.parse_args()))
