#!/usr/bin/env python3
"""Check analog gain acquisition in native, converted RTL and IHP mapped gates."""
from pathlib import Path
from hardware import configure_cleanup
from pma_test_pattern import arguments, run
from receive_gain_control_reference import generate

if __name__ == '__main__':
    configure_cleanup()
    source = Path(__file__).resolve()
    run(arguments(), source=source, top='ReceiveAnalogGainControl', generate_vectors=generate,
        reference=source.with_name('receive_gain_control_reference.py'), clock_hz=250000000,
        scope='gain acquisition, settling and faults against independent ADC vectors; no analog-loop or physical timing proof')
