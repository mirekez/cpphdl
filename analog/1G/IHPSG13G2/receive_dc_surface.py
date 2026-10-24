"""Characterize receiver transfer against its achieved, loaded supply voltage."""
import bisect
import math
import re

SUPPLIES = (3.0, 3.15, 3.3, 3.45, 3.6)
DIFFERENCES = (-.24, -.12, 0, .12, .24)


def surface_deck(original):
    source = re.findall(r'^Vsource0 (\S+) 0 DC ([-+0-9.eE]+)$', original, re.M)
    if len(source) != 1 or not math.isclose(float(source[0][1]), 3.3):
        raise ValueError('changed receive supply-source interface')
    result, count = re.subn(r'(?m)^\.save (.+)$', r'.save \1 v(' + source[0][0] + ')', original)
    if count != 1:
        raise ValueError('changed DC observation-save interface')
    result, count = re.subn(r'(?m)^dc Vprocess_difference -[.]24 [.]24 [.]12$',
        'dc Vprocess_difference -.24 .24 .12 Vsource0 3.0 3.6 .15', result)
    if count != 1:
        raise ValueError('changed receive differential DC sweep')
    output = 'wrdata dc.dat observation0 observation1 observation2 observation3 observation4 observation5'
    if result.count(output) != 1 or result.count('.control\n') != 1:
        raise ValueError('changed receive DC output interface')
    result = result.replace('.control\n', '.control\nset num_threads=1\n')
    return result.replace(output, 'let characterization_supply = v(' + source[0][0] + ')\n'
        'wrdata supply_surface.dat characterization_supply observation0 observation1 '
        'observation2 observation3 observation4 observation5')


class Surface:
    """Interpolate differential input first, then actual rail at that input.

    The mesh uses source-supply settings, but its interpolation coordinate is
    the solved internal rail. This retains voltage loss in the shared supply
    resistance and its dependence on input level and component loading.
    """
    def __init__(self, rows):
        if len(rows) != 25 or any(len(row) != 8 or not all(math.isfinite(v) for v in row) for row in rows):
            raise ValueError('incomplete or nonfinite receive DC surface')
        self.curves = []
        for source_supply in SUPPLIES:
            curve = sorted((row for row in rows if abs(row[1] - source_supply) < 1e-8), key=lambda row: row[0])
            if len(curve) != 5 or any(abs(row[0] - difference) > 1e-8
                    or abs(row[2] - (1.65 + difference / 2)) > .002
                    or abs(row[3] - (1.65 - difference / 2)) > .002
                    or not 0 < row[6] <= source_supply + 1e-6
                    or not 1.08 < row[7] < 1.35 for row, difference in zip(curve, DIFFERENCES)):
                raise ValueError('changed DC bias, input sweep or supply delivery')
            if any(b[5] - a[5] < .05 for a, b in zip(curve, curve[1:])):
                raise ValueError('DC buffer transfer is nonmonotonic or collapsed')
            self.curves.append(curve)
        for a, b in zip(self.curves, self.curves[1:]):
            if any(y[6] - x[6] < .05 for x, y in zip(a, b)):
                raise ValueError('DC supply axis is nonmonotonic or collapsed')

    def __call__(self, plus, minus, supply):
        if (not all(math.isfinite(v) for v in (plus, minus, supply))
                or abs((plus + minus) / 2 - 1.65) > .002):
            raise ValueError('receive input common mode is outside characterized range')
        difference = plus - minus
        index = min(max(1, bisect.bisect_left(DIFFERENCES, difference)), 4)
        fraction = (difference - DIFFERENCES[index - 1]) / (DIFFERENCES[index] - DIFFERENCES[index - 1])
        if not -.03 <= fraction <= 1.03:
            raise ValueError('receive differential input is outside characterized range')
        points = []
        for curve in self.curves:
            a, b = curve[index - 1], curve[index]
            points.append(tuple(a[col] + fraction * (b[col] - a[col]) for col in (6, 4, 5)))
        if not points[0][0] <= supply <= points[-1][0]:
            raise ValueError('achieved receiver supply is outside characterized range')
        index = min(max(1, bisect.bisect_left([row[0] for row in points], supply)), 4)
        a, b = points[index - 1], points[index]
        fraction = (supply - a[0]) / (b[0] - a[0])
        return tuple(a[col] + fraction * (b[col] - a[col]) for col in (1, 2))
