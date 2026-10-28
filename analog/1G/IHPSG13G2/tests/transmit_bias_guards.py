#!/usr/bin/env python3
"""Reject incomplete, nonmonotonic and electrically wrong TX bias sweeps."""
import copy
from pathlib import Path
import tempfile

from transmit_bias import measure


def main():
    rows = [[float(code), .9 + .001 * code, 1.8, -.002,
             .9 + .001 * code, 1.8, -.002] for code in range(64)]
    cases = {}
    cases['missing_code'] = rows[:-1]
    for name, column, value in [('nonmonotonic', 1, rows[31][1]), ('bad_cascode', 2, 2.),
                                ('reverse_current', 3, .002), ('nonfinite', 1, float('nan'))]:
        bad = copy.deepcopy(rows)
        bad[32][column] = value
        cases[name] = bad
    bad = copy.deepcopy(rows)
    for row in bad:
        row[1] *= 1.06
    cases['excess_wiring_error'] = bad
    with tempfile.TemporaryDirectory(prefix='bias-guards-') as directory:
        path = Path(directory) / 'trace.dat'
        def write(values):
            path.write_text('code bias_p cas_p current_p bias_r cas_r current_r\n' +
                            '\n'.join(' '.join(map(str, row)) for row in values) + '\n')
        write(rows)
        measure(path)
        for name, bad in cases.items():
            write(bad)
            try:
                measure(path)
            except ValueError:
                pass
            else:
                raise ValueError('accepted invalid bias result: ' + name)
    print('PASS valid bias sweep and six damaged-result guards')


if __name__ == '__main__':
    main()
