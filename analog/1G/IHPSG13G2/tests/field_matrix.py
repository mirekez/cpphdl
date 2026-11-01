#!/usr/bin/env python3
"""Reject incomplete and nonphysical field data before capacitance comparison."""
from pathlib import Path
import sys

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))
from field_matrix import read_matrix, coupling, grounded_pair

# Independent capacitor network: A-B = 2 fF, A-ground = 3 fF,
# B-ground = 4 fF. The field mesh coordinates are in micrometres.
GOOD = '''Dimension 3 x 3
g1_sub! 8e-9 -3e-9 -4e-9
g2_A -3e-9 5.1e-9 -2e-9
g3_B -4e-9 -2e-9 6.1e-9
Weighted Frobenius norm of the difference between capacitance (auto option): 0.001
Total time: 1s
'''
assert abs(coupling(read_matrix(GOOD, 1e-6, .002), 'A', 'B') - 2e-15) < 1e-28
assert abs(coupling(read_matrix(GOOD, 2e-6, .002), 'A', 'B') - 4e-15) < 1e-28
pair = grounded_pair(read_matrix(GOOD, 1e-6, .002), 'A', 'B')
# Include each conductor's independent 0.1 fF to infinity. Dropping the row
# sum would silently turn the 3.1/4.1 fF ground terms into 3/4 fF.
for signal, ground, quiet, opposite in [('A', 3.1, 5.1, 7.1), ('B', 4.1, 6.1, 8.1)]:
    for key, expected in [('ground_F', ground), ('same_direction_F', ground),
                          ('quiet_neighbor_F', quiet), ('opposite_direction_F', opposite)]:
        assert abs(pair['signals'][signal][key] - expected * 1e-15) < 1e-28
for first, second, ground in [('A', 'A', 'sub!'), ('A', 'B', 'missing')]:
    try:
        grounded_pair(read_matrix(GOOD, 1e-6, .002), first, second, ground)
    except ValueError:
        pass
    else:
        raise AssertionError('accepted an invalid two-signal grounding boundary')
assert read_matrix('Going out-of-core.\n' + GOOD, 1e-6, .002, allow_disk=True)['matrix_F']
COARSE_WARNING = 'Warning: capacitance matrix is not diagonally dominant due to row 2\n'
refined = GOOD + COARSE_WARNING + 'Iteration number #1\n' + GOOD
assert len(read_matrix(refined, 1e-6, .002)['discarded_coarse_mesh_diagnostics']) == 1
bad = [GOOD.replace('Total time:', 'Interrupted:'), GOOD.replace(': 0.001', ': 0.003'),
       GOOD.replace('5.1e-9', 'nan'), GOOD.replace('5.1e-9', '1e-9'),
       GOOD.replace('g2_A', 'g1_sub!'), GOOD.replace('g3_B -4e-9 -2e-9', 'g3_B -4e-9 -1e-9'),
       GOOD.replace('g2_A -3e-9', 'g2_A 3e-9'), GOOD + 'Warning: failed geometry\n',
       GOOD.replace('Dimension 3 x 3', 'Dimension 3 x 2'), GOOD + COARSE_WARNING,
       GOOD + 'Warning: invalid geometry\nIteration number #1\n' + GOOD,
       'Going out-of-core.\n' + GOOD]
for output in bad:
    try:
        read_matrix(output, 1e-6, .002)
    except ValueError:
        continue
    raise AssertionError('accepted damaged field result')
print('PASS field matrix: independent capacitor network, units, refinement and twelve rejected results')
