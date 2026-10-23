#!/usr/bin/env python3
"""Check range-to-Q8.8 calibration in native C++RTL, converted RTL and IHP gates."""
from pathlib import Path
from hardware import configure_cleanup
from pma_test_pattern import arguments, run
from receive_range_calibration_reference import generate

if __name__ == '__main__':
    configure_cleanup()
    source = Path(__file__).resolve()
    run(arguments(), source=source, top='ReceiveRangeCalibration', generate_vectors=generate,
        reference=source.with_name('receive_range_calibration_reference.py'), clock_hz=250000000,
        scope='exact rounded range calibration, latency, restart and faults; no analog measurement or physical timing proof')
