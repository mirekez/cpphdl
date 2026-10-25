"""Retain every electrical state at nondecreasing solver times."""
from array import array
import bisect
import csv
import gzip
import json
import math
from pathlib import Path
from trace_codec import verify_roundoff_audit


def read_states(path, header, *, csv_format=False, audit=None):
    path = Path(path)
    records = []
    if audit is not None:
        verify_roundoff_audit(path, audit)
        records = json.loads(Path(audit).read_text())['duplicate_records']
    result, duplicate_index = [], 0

    def append(fields):
        values = array('d', (float(value) for value in fields))
        if len(values) != len(header) or not all(math.isfinite(value) for value in values):
            raise ValueError('nonrectangular or nonfinite solver state')
        if result and values[0] < result[-1][0]:
            raise ValueError('backward solver time')
        result.append(values)

    with (gzip.open(path, 'rt') if path.suffix == '.gz' else path.open()) as source:
        if csv_format:
            if audit is not None:
                raise ValueError('SPICE audit cannot describe CSV states')
            reader = csv.reader(source)
            if next(reader) != header:
                raise ValueError('CSV electrical interface changed')
        else:
            names = next(source).split()
            if len(names) != len(header) or names[0] != 'time':
                raise ValueError('SPICE electrical interface changed')
            reader = (line.split() for line in source if line.strip())
        canonical_count = 0
        for fields in reader:
            while duplicate_index < len(records) and records[duplicate_index]['after_rows'] == canonical_count:
                append(records[duplicate_index]['original_line'].split())
                duplicate_index += 1
            append(fields)
            canonical_count += 1
        while duplicate_index < len(records) and records[duplicate_index]['after_rows'] == canonical_count:
            append(records[duplicate_index]['original_line'].split())
            duplicate_index += 1
    if duplicate_index != len(records) or len(result) < 10 or result[-1][0] <= result[0][0]:
        raise ValueError('incomplete solver trajectory or audit')
    return result


class StateEnvelope:
    """Bound all states at the sample or neighboring interpolation endpoints."""
    def __init__(self, rows):
        if not rows:
            raise ValueError('empty solver trajectory')
        self.rows = rows
        self.times = [row[0] for row in rows]
        if any(b < a for a, b in zip(self.times, self.times[1:])):
            raise ValueError('backward solver time')

    def bounds(self, time, column):
        return self.projected_bounds(time, lambda row: row[column])

    def projected_bounds(self, time, project):
        # Project correlated pins together before taking their envelope.
        if not math.isfinite(time) or not self.times[0] <= time <= self.times[-1]:
            raise ValueError('sample outside solver trajectory')
        left = bisect.bisect_left(self.times, time)
        right = bisect.bisect_right(self.times, time)
        if left != right:
            values = [project(row) for row in self.rows[left:right]]
            return min(values), max(values)
        a, b = self.times[left - 1], self.times[left]
        before = self.rows[bisect.bisect_left(self.times, a):left]
        after = self.rows[left:bisect.bisect_right(self.times, b)]
        fraction = (time - a) / (b - a)
        x, y = [project(row) for row in before], [project(row) for row in after]
        return (min(x) * (1 - fraction) + min(y) * fraction,
                max(x) * (1 - fraction) + max(y) * fraction)
