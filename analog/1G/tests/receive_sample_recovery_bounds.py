#!/usr/bin/env python3
"""Finite endpoint proof for history bounds and coefficient quantization."""
import json

Q = 1 << 24
PERIOD = (16743662, 16810770)
INTERVAL = (16088302, 17466130)


def check():
    # All expressions are affine between integer boundaries, so extrema occur
    # at these domain endpoints. Four-tap bases must lie in the eight samples.
    bases = [2 + (fraction + sign * period) // Q
             for fraction in (0, Q - 1) for sign in (-1, 0, 1) for period in PERIOD]
    assert min(bases) == 0 and max(bases) == 4
    advances = [(fraction + 2 * interval) // Q
                for fraction in (0, Q - 1) for interval in INTERVAL]
    assert min(advances) == 1 and max(advances) == 3
    # At every representable interpolation fraction, compare the fixed basis
    # to exact real cubic coefficients. An arbitrary byte vector has error at
    # most 255 times the positive coefficient-error sum (zero total error).
    maximum = 0
    for fraction in range(256):
        t = fraction / 256
        t2 = (fraction * fraction + 8) >> 4
        t3 = (t2 * fraction + 128) >> 8
        actual = [(-16 * fraction + 2 * t2 - t3) / 8192,
                  (8192 - 5 * t2 + 3 * t3) / 8192,
                  (16 * fraction + 4 * t2 - 3 * t3) / 8192,
                  (-t2 + t3) / 8192]
        exact = [(-t + 2 * t*t - t*t*t) / 2,
                 1 - 2.5 * t*t + 1.5 * t*t*t,
                 (t + 4 * t*t - 3 * t*t*t) / 2,
                 (-t*t + t*t*t) / 2]
        assert sum(actual) == 1
        maximum = max(maximum, 255 * sum(max(a-b, 0) for a, b in zip(actual, exact)))
    # For arbitrary bytes, cubic derivative <= 1.5 * full scale per sample.
    # Truncating the phase by less than 1/256 sample contributes <1.495 counts.
    # Nearest-integer output rounding contributes at most half a count.
    bound = maximum + 1.5 * 255 / 256 + .5
    assert bound < 2.25
    return dict(status='pass', history_base_range=[min(bases), max(bases)],
                center_advance_range=[min(advances), max(advances)],
                maximum_basis_error_counts=maximum,
                arbitrary_byte_interpolation_error_bound_counts=bound,
                fraction_resolution_ps=4000 / 256)


if __name__ == '__main__':
    print(json.dumps(check(), indent=2))
