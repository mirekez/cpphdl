#!/usr/bin/env python3
"""Route and verify the reset controller from its current functional proof."""
import argparse
import json
from pathlib import Path
import tempfile

from physical_module import ANALOG, configure_cleanup, implement


def mapped_work(proof):
    evidence = json.loads(Path(proof).read_text())
    if (evidence.get('status') != 'pass' or evidence.get('module') != 'PHYResetControl' or
            evidence.get('events') != 153600 or evidence.get('schedules') != 10240 or
            not evidence.get('native_rtl_exact') or not evidence.get('native_mapped_exact')):
        raise ValueError('complete current PHYResetControl functional proof required')
    work = Path(evidence['work']).resolve()
    if json.loads((work / 'hardware_checks.json').read_text()) != evidence:
        raise ValueError('reset proof does not match retained mapped evidence')
    return work


if __name__ == '__main__':
    configure_cleanup()
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--proof', type=Path, required=True)
    parser.add_argument('--work-root', type=Path, required=True)
    parser.add_argument('--pdk', type=Path, default=ANALOG / 'pdk/IHPSG13G2')
    parser.add_argument('--tools', type=Path, default=ANALOG / 'out/repeater-toolchain')
    parser.add_argument('--verilator', required=True)
    parser.add_argument('--cxx', default='/usr/bin/c++')
    args = parser.parse_args()
    args.mapped_work = mapped_work(args.proof)
    args.module = 'PHYResetControl'
    args.bounded_clock_run = True
    args.max_transition = .3
    args.work_root.mkdir(parents=True, exist_ok=True)
    args.work = Path(tempfile.mkdtemp(prefix='PHYResetControl-', dir=args.work_root.resolve()))
    print('Physical work: ' + str(args.work), flush=True)
    raise SystemExit(implement(args))
