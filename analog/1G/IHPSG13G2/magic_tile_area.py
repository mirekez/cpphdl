"""Bound Magic's area allocation to the actual resistor tile rectangle.

The pinned extractor permits a terminal breakpoint beyond a tile boundary.
Its unbounded end-area subtraction can then emit negative rnode capacitance,
which ext2spice drops. Clip the area partition boundaries, not the resistor
coordinates, to keep nonnegative weights with exactly the original tile area.
This retains the extractor's area-distribution approximation.
"""
import hashlib

FUNCTION = r'''
static void
ResDistributeTileArea(Breakpoint *first, int lower, int upper, int transverse,
                     bool eastwest)
{
    Breakpoint *point;
    double previous = lower, boundary, a, b;
    for (point = first; point != NULL; point = point->br_next)
    {
        boundary = upper;
        if (point->br_next != NULL)
        {
            a = eastwest ? point->br_loc.p_x : point->br_loc.p_y;
            b = eastwest ? point->br_next->br_loc.p_x : point->br_next->br_loc.p_y;
            boundary = (a + b) / 2.0;
            if (boundary < lower) boundary = lower;
            if (boundary > upper) boundary = upper;
        }
        point->br_this->rn_float.rn_area += (boundary - previous) * transverse;
        previous = boundary;
    }
}
'''


def apply(source):
    path = source / 'resis/ResMakeRes.c'
    before = path.read_bytes()
    text = before.decode()

    def replace(old, new, count=1):
        nonlocal text
        if text.count(old) != count:
            raise ValueError('pinned Magic tile partition differs: ' + old)
        text = text.replace(old, new)

    replace('bool\nResCalcEastWest(', FUNCTION + '\nbool\nResCalcEastWest(')
    replace('    float\trArea;\n', '', 2)
    for axis, direction, lower, upper, width in [
        ('x', 'TRUE', 'LEFT', 'RIGHT', 'height'), ('y', 'FALSE', 'BOTTOM', 'TOP', 'width')]:
        line = f'    count = ResSortBreaks(&info->breakList, {direction});'
        replace(line, line + '\n    ResDistributeTileArea(info->breakList, '
                f'{lower}(tile), {upper}(tile), {width}, {direction});')
        replace(f'    p2->br_this->rn_float.rn_area += {width} * (p2->br_loc.p_{axis} - {lower}(tile));\n', '')
        replace(f'    p2->br_this->rn_float.rn_area += {width} * ({upper}(tile) - p2->br_loc.p_{axis});\n', '')
        replace(f'\t    rArea = ((p2->br_loc.p_{axis} - p1->br_loc.p_{axis}) * {width}) / 2;\n', '')
    replace('\t    resistor->rr_connection1->rn_float.rn_area += rArea;\n', '', 2)
    replace('\t    resistor->rr_connection2->rn_float.rn_area += rArea;\n', '', 2)
    path.write_text(text)
    return dict(file='resis/ResMakeRes.c', before_sha256=hashlib.sha256(before).hexdigest(),
        after_sha256=hashlib.sha256(path.read_bytes()).hexdigest(),
        reason='clip area partition to tile bounds; retain resistor geometry and total conductor area')
