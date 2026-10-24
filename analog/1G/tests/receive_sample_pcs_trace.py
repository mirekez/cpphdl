#!/usr/bin/env python3
"""Independent complete-clock audit of sampled PCS native/RTL evidence."""
import argparse
import gzip
import json
from pathlib import Path


def audit(path, fixture_directory):
    opener = gzip.open if path.suffix == '.gz' else open
    expected = []
    for role in (0, 1):
        with (fixture_directory / f'role{role}.txt').open() as fixture:
            master, symbols, frames = map(int, fixture.readline().split())
        if master != role or frames != 2:
            raise ValueError('malformed independent source manifest')
        for ppm in (-1500, 0, 1500):
            for delay in (0, .105, .9):
                expected.append(int((symbols - 40) * 2 * (1 + ppm * 1e-6)) + 1)
    counts = [0] * 18
    with opener(path, 'rt') as trace:
        previous = 0
        for line in trace:
            values = list(map(int, line.rstrip().split(',')))
            if len(values) != 14 or any(x < 0 for x in values):
                raise ValueError('malformed complete-clock row')
            case, cycle, word, tag, valid, pma, reset, output, output_valid, fault, locked, symbol_valid, symbol_tag, fraction = values
            if case >= 18 or case < previous or case > previous + 1 or cycle != counts[case]:
                raise ValueError('missing, reordered or repeated clock')
            previous = case
            if reset != (cycle == 0) or valid != (cycle != 0) or pma != (cycle > 34000):
                raise ValueError('wrong source/reset/PMA schedule')
            if cycle and tag != ((65520 + cycle - 1) & 65535):
                raise ValueError('sample tag schedule differs')
            if fault or output >= 1024 or word >= 2**32 or fraction >= 2**24 or symbol_tag >= 2**16:
                raise ValueError('invalid output range or transport fault')
            if any(x not in (0, 1) for x in (output_valid, locked, symbol_valid)):
                raise ValueError('invalid Boolean output')
            counts[case] += 1
    if counts != expected:
        raise ValueError('incomplete declared sample sequences')
    return dict(status='pass', clocks=sum(counts), case_clocks=counts)


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('trace', type=Path)
    parser.add_argument('fixture_directory', type=Path)
    args = parser.parse_args()
    print(json.dumps(audit(args.trace, args.fixture_directory), indent=2))
