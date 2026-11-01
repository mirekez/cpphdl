#!/usr/bin/env python3
"""Reject incomplete or electrically unacceptable acquisition observations."""
from array import array
from pathlib import Path
import sys

sys.path.insert(0, str(Path(__file__).resolve().parents[3] / 'VerilogA'))
from compact_trace import Rows
from oscillator_acquisition_window import measure, START, STOP


def waveform(jitter=False, supply=1.2, high=1.2):
    data = array('d')
    for i in range(round(STOP / 10e-12) + 1):
        time = i * 10e-12
        shifted = time - (.2e-9 if jitter and time > START + 500e-9 else 0)
        phase = shifted % 8e-9
        level = high * max(0., min(1., phase / .1e-9, (4.1e-9 - phase) / .1e-9))
        data.extend((time, level, level, .8, supply, 0.))
    return Rows(data, 6)


rows = waveform()
assert measure(rows)['status'] == 'pass'
for damaged in (rows[:-1], rows[1:], rows[::2]):
    try:
        measure(damaged)
    except ValueError:
        pass
    else:
        raise AssertionError('accepted incomplete or unresolved waveform')
assert not measure(waveform(jitter=True))['checks']['steady']
assert not measure(waveform(supply=1.09))['checks']['supply']
assert not measure(waveform(high=1.0))['checks']['rails']
rows.data[-3] = float('nan')
try:
    measure(rows)
except ValueError:
    pass
else:
    raise AssertionError('accepted nonfinite control voltage')
print('PASS acquisition window: good waveform and seven damaged trajectories')
