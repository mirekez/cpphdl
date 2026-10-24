"""Apply the transmitter limits to every state at each sampling aperture."""
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parents[2] / 'VerilogA'))
from trace_states import StateEnvelope
from core_transmit_driver import check as core_check


def check_apertures(rows, words, interface):
    envelope = StateEnvelope(rows)
    load = interface['load_ohm']
    scale = 2 if load == 0 else .04 / (.02 + 2 / load)
    plateaus, maximum_error = [], 0.
    for index, word in enumerate(words):
        if index < interface['warm_words']:
            continue
        time = interface['startup_s'] + (index + .8) * interface['ui_s']
        for bank, key in enumerate(('main_p', 'main_n', 'history_p', 'history_n')):
            for bit in range(4):
                low, high = envelope.bounds(time, 1 + bank * 4 + bit)
                core_low, core_high = envelope.bounds(time, 26 + bank * 4 + bit)
                active = bool(word[key] & (1 << bit))
                if (low < 3.0 if active else high > .3):
                    raise ValueError('steering gate fails in a retained aperture state')
                if (core_low < 1.05 if active else core_high > .15):
                    raise ValueError('core gate fails in a retained aperture state')
        weighted = 3 * (2 - word['code']) + (2 - word['previous']) if word['valid'] else 0
        expected = .125 * weighted * scale
        pin_error = envelope.projected_bounds(time, lambda row: row[17] - row[18] - row[19])
        if max(abs(value) for value in pin_error) > 1e-8:
            raise ValueError('retained differential observation disagrees with pins')
        plateau = [envelope.bounds(time, column) for column in (17, 18, 19, 20, 21, 22)]
        maximum_error = max(maximum_error, *(abs(value - expected) for value in plateau[2]))
        if interface['filtered'] and any(abs(value - expected) >= .020 for value in plateau[2]):
            raise ValueError('weighted level exceeds 20 mV in a retained aperture state')
        low, high = ((.035, .048) if word['valid'] else (.0005, .004))
        if not all(low < value / .25 < high for value in plateau[5]):
            raise ValueError('supply current fails in a retained aperture state')
        plateaus.append(plateau)
    if not plateaus:
        raise ValueError('no checked transmitter apertures')
    return dict(plateau_bounds_V=plateaus, maximum_all_state_plateau_error_V=maximum_error,
                checked_aperture_states='all equal-time states and neighboring-state interpolation envelopes')


def check(rows, words, interface):
    # The existing trajectory, bias, supply, trim, level coverage and refinement
    # requirements remain in force. The additional checks cover both sides of
    # every repeated-time state instead of discarding a solver record.
    result = core_check(rows, words, interface)
    result.update(check_apertures(rows, words, interface))
    return result
