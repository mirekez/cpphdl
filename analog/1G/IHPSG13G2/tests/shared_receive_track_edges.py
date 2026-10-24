"""Independent all-state edge audit: reject unrequested reopenings or pulses."""
from bisect import bisect_left
from shared_receive_track import HEADER, WARMUP, PERIOD


def edge_audit(header, rows):
    if header != HEADER:
        raise ValueError('changed TRACK edge interface')
    times = [row[0] for row in rows]
    columns = [HEADER.index('TRACK' + str(i)) for i in range(13)]
    mask, events = 0, []
    for event in range(32):
        selected = (event + 10) % 13
        closing = (1 << selected) | ((1 << ((selected + 11) % 13)) if event == 20 else 0)
        opening = ((closing << 2) | (closing >> 11)) & 8191
        allowed = event > 0 and event < 26 and event not in (7, 8)
        before = mask
        mask = ((before | (opening if allowed else 0)) & ~closing) if event not in (23, 24) else 0
        begin = WARMUP + event * PERIOD
        first, last = bisect_left(times, begin - 2e-9), bisect_left(times, begin + 2e-9)
        if first == 0 or last <= first:
            raise ValueError('missing complete edge audit interval')
        observed, expected, crossing_times = [], [], []
        for i, column in enumerate(columns):
            if (before ^ mask) & (1 << i):
                expected.append((i, 'rise' if mask & (1 << i) else 'fall'))
            for a, b in zip(rows[first - 1:last - 1], rows[first:last]):
                old, new = a[column] - .5 * a[2], b[column] - .5 * b[2]
                if (old < 0) != (new < 0):
                    observed.append((i, 'rise' if new >= 0 else 'fall'))
                    crossing_times.append(b[0])
        # Global disable is deliberately commanded2ns before event23. All
        # ordinary state changes must follow their own commanded event.
        timely = event == 23 or all(t >= begin for t in crossing_times)
        events.append(dict(event=event, expected=expected, observed=observed,
                           crossing_offsets_s=[t - begin for t in crossing_times],
                           passed=timely and sorted(expected) == sorted(observed)))
    quiet = [row for row in rows if WARMUP - 17e-9 <= row[0] < WARMUP - 2e-9
             or row[0] >= WARMUP + 31.5 * PERIOD]
    quiet_clear = bool(quiet) and all(row[column] < .5 * row[2] for row in quiet for column in columns)
    return dict(status='pass' if quiet_clear and all(e['passed'] for e in events) else 'fail',
                quiet_clear=quiet_clear, events=events,
                scope='all13 TRACK half-rail crossings; rejects extra pulses and reopening; fixed2ns selection setup interval')
