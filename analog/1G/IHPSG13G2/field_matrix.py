"""Read a completed FasterCap Maxwell matrix, with explicit geometry units."""
import math
import re


def read_matrix(output, coordinate_unit_m, tolerance, allow_disk=False):
    if (not math.isfinite(coordinate_unit_m) or coordinate_unit_m <= 0
            or not 0 < tolerance < 1):
        raise ValueError('invalid field-solver units or convergence limit')
    # The pinned Linux binary fails the independent sphere oracle when its
    # interaction matrix is stored on disk. A converged log is not sufficient.
    if 'Going out-of-core.' in output and not allow_disk:
        raise ValueError('disk-backed FasterCap mode has not passed the independent electrostatic oracle')
    blocks = list(re.finditer(r'^Dimension (\d+) x (\d+)\s*\n((?:g\d+_[^\n]+\n)+)', output, re.M))
    if not blocks:
        raise ValueError('missing Maxwell matrix')
    # The initial coarse mesh can yield a nonphysical matrix. Only these two
    # explicit diagnostics may be superseded by a later, checked refinement.
    # Geometry warnings and errors must never be hidden by subsequent matrices.
    discarded = []
    for diagnostic in re.finditer(r'^\s*((?:warning|error)\b[^\n]*)', output, re.I | re.M):
        line = diagnostic[1]
        if line == 'Warning: dummy dielectric-dielectric interface found in the input file, skipping':
            continue
        coarse_warning = re.fullmatch(r'Warning: capacitance matrix (?:has a non-negative off-diagonal '
            r'element at row \d+ col \d+|is not diagonally dominant due to row \d+)', line)
        later_iteration = re.search(r'^Iteration number #\d+',
            output[diagnostic.end():blocks[-1].start()], re.M)
        if coarse_warning and later_iteration:
            discarded.append(line)
        else:
            raise ValueError('unreviewed field-solver diagnostic: ' + line)
    convergence = re.findall(r'Weighted Frobenius norm[^\n]*:\s*([-+\deE.]+)', output)
    if (not convergence or not math.isfinite(float(convergence[-1]))
            or float(convergence[-1]) >= tolerance or 'Total time:' not in output):
        raise ValueError('field solve did not finish within its convergence limit')
    rows, columns, text = blocks[-1].groups()
    size = int(rows)
    entries = [line.split() for line in text.splitlines()]
    names = [row[0] for row in entries]
    if (size < 2 or size != int(columns) or len(entries) != size or len(set(names)) != size
            or any(len(row) != size + 1 for row in entries)):
        raise ValueError('invalid Maxwell matrix dimensions or conductor names')
    matrix = [[float(value) * coordinate_unit_m for value in row[1:]] for row in entries]
    if any(not math.isfinite(value) for row in matrix for value in row):
        raise ValueError('nonfinite capacitance')
    asymmetry = 0.
    for i in range(size):
        if matrix[i][i] <= 0 or sum(matrix[i]) < -1e-5 * matrix[i][i]:
            raise ValueError('nonphysical diagonal or negative capacitance to infinity')
        for j in range(size):
            if i == j:
                continue
            if matrix[i][j] > 0:
                raise ValueError('positive off-diagonal Maxwell coefficient')
            scale = max(abs(matrix[i][j]), abs(matrix[j][i]))
            if scale:
                asymmetry = max(asymmetry, abs(matrix[i][j] - matrix[j][i]) / scale)
    if asymmetry > .01:
        raise ValueError('Maxwell matrix reciprocity differs by more than one percent')
    return dict(conductors=names, matrix_F=matrix, maximum_relative_asymmetry=asymmetry,
                final_relative_matrix_change=float(convergence[-1]), coordinate_unit_m=coordinate_unit_m,
                discarded_coarse_mesh_diagnostics=discarded)


def coupling(matrix, first, second):
    names = matrix['conductors']
    def index(net):
        matches = [i for i, name in enumerate(names) if name.split('_', 1)[1] == net]
        if len(matches) != 1:
            raise ValueError('missing or ambiguous conductor: ' + net)
        return matches[0]
    i, j = index(first), index(second)
    if i == j:
        raise ValueError('coupling needs two different conductors')
    values = matrix['matrix_F']
    return -(values[i][j] + values[j][i]) / 2


def grounded_pair(matrix, first, second, ground='sub!'):
    """Reduce two signal conductors with substrate and infinity at AC ground.

    Infinity is the reference potential of a Maxwell matrix. Its nonzero row
    sum must remain in the ground term rather than disappearing from the load.
    Preserve both diagonal terms while symmetrizing the mutual coefficient.
    """
    names = [name.split('_', 1)[1] for name in matrix['conductors']]
    if len(names) != 3 or len({first, second, ground}) != 3 or set(names) != {first, second, ground}:
        raise ValueError('grounded pair requires exactly two signals and one substrate conductor')
    mutual = coupling(matrix, first, second)
    result = dict(mutual_F=mutual, signals={})
    for signal in (first, second):
        index = names.index(signal)
        total = matrix['matrix_F'][index][index]
        grounded = total - mutual
        if mutual <= 0 or grounded <= 0:
            raise ValueError('grounded pair has a nonphysical capacitance')
        result['signals'][signal] = dict(ground_F=grounded,
            same_direction_F=grounded, quiet_neighbor_F=total,
            opposite_direction_F=grounded + 2 * mutual)
    return result
