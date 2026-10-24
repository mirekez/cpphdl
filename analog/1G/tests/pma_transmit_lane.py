#!/usr/bin/env python3
"""Check the actual shared normal/test transmit lane in native, RTL and gates."""
from pathlib import Path
from pma_test_pattern import arguments, configure_cleanup, run
from pma_transmit_lane_reference import generate

if __name__ == '__main__':
    configure_cleanup()
    source = Path(__file__).resolve()
    run(arguments(), source=source, top='PMATransmitLane', generate_vectors=generate,
        reference=source.with_name('pma_transmit_lane_reference.py'))
