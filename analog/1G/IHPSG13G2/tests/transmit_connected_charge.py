"""Use the unchanged complete-TX checks for candidate current-cell PSP charge."""
import argparse
from pathlib import Path
import sys

SOURCE = Path(__file__).resolve()
ROOT = SOURCE.parents[3]
sys.path[:0] = [str(SOURCE.parents[1]), str(SOURCE.parent), str(ROOT / 'VerilogA'),
               str(ROOT / '1G/tests'), str(ROOT / 'pdk/1G_repeater_IHPSG13G2')]
import transmit_connected
import transmit_connected_startup
from transmit_charge_models import load_charge_models
from tool_process import configure_cleanup


def run(args):
    # Select the model loader only inside this test process. The shared
    # startup, full-window checks and physical/schematic tolerances are reused.
    original = transmit_connected.load_models
    transmit_connected.load_models = lambda directory, proof_root: load_charge_models(
        directory, proof_root, args.charge_storage)
    try:
        if args.startup:
            transmit_connected_startup.run(args)
        else:
            transmit_connected.run(args)
    finally:
        transmit_connected.load_models = original


if __name__ == '__main__':
    configure_cleanup()
    parser = argparse.ArgumentParser(description=__doc__)
    for name in ('exporter', 'fixture', 'proofs', 'pdk', 'ngspice', 'work', 'charge-storage'):
        parser.add_argument('--' + name, type=Path, required=True)
    parser.add_argument('--corner', choices=('mos_tt', 'mos_ss', 'mos_ff'), default='mos_tt')
    parser.add_argument('--temperature', type=float, default=27)
    parser.add_argument('--trim-code', type=int, choices=range(64), default=44)
    parser.add_argument('--cxx', default='/usr/bin/c++')
    parser.add_argument('--startup', action='store_true')
    parser.add_argument('--emit-only', action='store_true')
    parser.add_argument('--concurrent', action='store_true', help='use a separately bounded startup budget')
    run(parser.parse_args())
