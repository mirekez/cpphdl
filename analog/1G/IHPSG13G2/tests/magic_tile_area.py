#!/usr/bin/env python3
"""Check the compiled extractor partition against an independent area integral."""
from pathlib import Path
import subprocess
import sys
import tempfile
import unittest

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))
from magic_tile_area import FUNCTION


class TileAreaTests(unittest.TestCase):
    def test_area_integral_inside_and_outside_tile(self):
        # Half-grid samples integrate the nearest-terminal regions exactly:
        # integer terminal coordinates put every bisector on a half-grid edge.
        fixture = r'''
#include <stdbool.h>
#include <stddef.h>
#include <math.h>
#include <stdio.h>
typedef struct { struct { double rn_area; } rn_float; } Node;
typedef struct Breakpoint {
    struct { int p_x, p_y; } br_loc;
    Node *br_this;
    struct Breakpoint *br_next;
} Breakpoint;
'''
        main = r'''
int main(void) {
    int trials = 0;
    for (int axis = 0; axis < 2; ++axis)
    for (int a = -60; a < 140; a += 7)
    for (int b = a + 1; b < 150; b += 11)
    for (int c = b + 1; c < 160; c += 17) {
        int coords[3] = {a, b, c};
        Node nodes[3] = {0};
        Breakpoint points[3];
        double expected[3] = {0};
        for (int i = 0; i < 3; ++i) {
            points[i].br_loc.p_x = coords[i];
            points[i].br_loc.p_y = coords[i];
            points[i].br_this = &nodes[i];
            points[i].br_next = i == 2 ? NULL : &points[i + 1];
        }
        ResDistributeTileArea(points, 0, 100, 13, axis);
        for (int j = 0; j < 200; ++j) {
            double x = j / 2.0 + .25;
            int nearest = 0;
            for (int i = 1; i < 3; ++i)
                if (fabs(x - coords[i]) < fabs(x - coords[nearest])) nearest = i;
            expected[nearest] += 6.5;
        }
        double total = 0;
        for (int i = 0; i < 3; ++i) {
            if (nodes[i].rn_float.rn_area != expected[i] || nodes[i].rn_float.rn_area < 0)
                return 1;
            total += nodes[i].rn_float.rn_area;
        }
        if (total != 1300) return 2;
        ++trials;
    }
    printf("PASS %d conductor area integrals\n", trials);
    return 0;
}
'''
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            source, binary = root / 'partition.c', root / 'partition'
            source.write_text(fixture + FUNCTION + main)
            subprocess.run(['/usr/bin/cc', '-std=c99', '-Wall', '-Werror', source, '-lm', '-o', binary], check=True)
            result = subprocess.check_output([binary], text=True)
            self.assertRegex(result, r'^PASS [1-9][0-9]+ conductor area integrals\n$')
            print(result.strip())


if __name__ == '__main__':
    unittest.main()
