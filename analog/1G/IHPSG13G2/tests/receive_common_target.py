"""Explicit divider-target specialization of the unchanged reference contract."""
from bisect import bisect_left, bisect_right

from receive_common_reference import contract as original_contract


def contract(rows, *, numerator, denominator):
    # A declared resistor ratio is mandatory; never fit the target to OUT.
    if (type(numerator) is not int or type(denominator) is not int or
            not 0 < numerator < denominator):
        raise ValueError('an explicit positive divider ratio below one is required')
    result = original_contract(rows)
    times = [row[0] for row in rows]
    settled = []
    for index in range(32):
        start = 16e-6 + (8 * index + 6) * 1e-9
        end = 16e-6 + (8 * index + 7) * 1e-9
        settled.extend(rows[bisect_left(times, start):bisect_right(times, end)])
    accuracy = max(abs(row[1] - row[2] * numerator / denominator) for row in settled)
    transient = max(abs(row[1] - row[2] * numerator / denominator) for row in rows)
    divider = max(abs(row[3] - row[2] * numerator / denominator) for row in rows)
    result['checks'].update(settled_reference=accuracy <= .015,
                            transient_reference=transient <= .100,
                            divider=divider <= .002)
    result.update(maximum_settled_error_V=accuracy,
                  maximum_transient_error_V=transient,
                  maximum_divider_error_V=divider,
                  declared_target=dict(numerator=numerator, denominator=denominator,
                                       nominal_at_3p3V=3.3 * numerator / denominator))
    return result
