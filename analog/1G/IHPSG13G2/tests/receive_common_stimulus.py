"""Verify the imposed sources through the bench's finite source impedances."""
from bisect import bisect_right
import math


def source_points(lane, polarity):
    points = [(0., 0.), (1e-6, 0.), (1.1e-6, 2.3)]
    for symbol in range(32):
        begin = 16e-6 + symbol * 8e-9
        mean = (2.1, 2.3, 2.5)[symbol % 3]
        half = .25 * ((symbol * 3 + lane) % 5 - 2)
        points += [(begin, points[-1][1]), (begin + .1e-9, mean + polarity * half)]
    return points


def source_profile(rows):
    """Audit every state; the electrical contract checks interval completeness."""
    if not rows:
        raise ValueError('missing common-mode source trajectory')
    sources = [source_points(lane, polarity) for lane in range(8) for polarity in (1, -1)]
    times = [[point[0] for point in points] for points in sources]
    maximum, worst, count = 0., None, 0
    for row in rows:
        if len(row) != 40 or not all(math.isfinite(value) for value in row):
            raise ValueError('malformed common-mode source state')
        for source, points in enumerate(sources):
            lane, leg = divmod(source, 2)
            terminal, sensed = row[8 + 4 * lane + leg], row[10 + 4 * lane + leg]
            # No charge is stored at P/N: source25 ohms and series500 ohms
            # give this KCL identity even while the sensed capacitor moves.
            achieved = terminal + (25 / 500) * (terminal - sensed)
            right = bisect_right(times[source], row[0])
            expected = points[0][1] if right == 0 else points[-1][1]
            if 0 < right < len(points):
                a, b = points[right - 1], points[right]
                expected = a[1] + (b[1] - a[1]) * (row[0] - a[0]) / (b[0] - a[0])
            error = abs(achieved - expected)
            if error > maximum:
                maximum = error
                worst = dict(time_s=row[0], lane=lane, leg='P' if leg == 0 else 'N',
                             reconstructed_source_V=achieved, declared_source_V=expected)
            count += 1
    return dict(checks=dict(source_profile=maximum <= .0005), maximum_source_error_V=maximum,
                tolerance_V=.0005, checked_terminal_states=count, worst=worst,
                scope='all eight imposed source pairs; declared32-symbol schedule and100 ps edges reconstructed through25/500 ohm KCL')
