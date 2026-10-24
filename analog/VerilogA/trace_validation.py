"""Validate an electrical trajectory without retaining a second copy."""
import math


def validate_rows(rows, width, label):
    """Check the strict comparison contract; consume iterators once.

    The caller owns the states. Validation retains only the previous time and
    does not round, interpolate, discard duplicates or change a measurement.
    Parsing a file into numeric rows remains a separate operation.
    """
    previous = None
    count = 0
    for row in rows:
        if len(row) != width:
            raise ValueError(f"{label}: expected {width} columns, got {len(row)}")
        time = None
        for column, field in enumerate(row):
            value = float(field)
            if not math.isfinite(value):
                raise ValueError(f"{label}: nonfinite or missing sample")
            if column == 0:
                time = value
        if time is None:
            raise ValueError(f"{label}: missing time column")
        if previous is not None and time <= previous:
            raise ValueError(f"{label}: non-increasing time")
        previous = time
        count += 1
    if count < 10:
        raise ValueError(f"{label}: incomplete trajectory")
    return count
