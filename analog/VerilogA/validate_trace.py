"""Validate an existing trajectory without allocating a normalized copy."""
import math


def validate_rows(rows, width, label):
    """Apply checked_rows' strict checks while retaining only the current row.

    Return the row count. The caller keeps ownership of its original rows;
    this function neither resamples them nor discards electrical states.
    """
    previous, count = None, 0
    for row in rows:
        if len(row) != width:
            raise ValueError(f"{label}: expected {width} columns, got {len(row)}")
        values = [float(value) for value in row]
        if not all(math.isfinite(value) for value in values):
            raise ValueError(f"{label}: nonfinite or missing sample")
        if previous is not None and values[0] <= previous:
            raise ValueError(f"{label}: non-increasing time")
        previous = values[0]
        count += 1
    if count < 10:
        raise ValueError(f"{label}: incomplete trajectory")
    return count
