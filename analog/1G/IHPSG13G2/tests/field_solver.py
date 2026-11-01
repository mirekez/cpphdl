#!/usr/bin/env python3
"""Check the field solver against isolated-sphere electrostatics and scaling."""
import argparse
import json
import math
import os
from pathlib import Path
import re
import sys
import tempfile

SOURCE = Path(__file__).resolve()
ANALOG = SOURCE.parents[3]
sys.path[:0] = [str(SOURCE.parents[1]), str(ANALOG / 'pdk/1G_repeater_IHPSG13G2')]
from prepare_fastercap import digest
from input_charge import verify_files
from tool_process import execute, configure_cleanup


def sphere(path, level, radius, translation=(0., 0., 0.)):
    vertices = [(1, 0, 0), (-1, 0, 0), (0, 1, 0), (0, -1, 0), (0, 0, 1), (0, 0, -1)]
    faces = [(0, 2, 4), (2, 1, 4), (1, 3, 4), (3, 0, 4),
             (2, 0, 5), (1, 2, 5), (3, 1, 5), (0, 3, 5)]
    for _ in range(level):
        midpoints = {}

        def midpoint(a, b):
            key = tuple(sorted((a, b)))
            if key not in midpoints:
                point = tuple(vertices[a][i] + vertices[b][i] for i in range(3))
                norm = math.sqrt(sum(x * x for x in point))
                midpoints[key] = len(vertices)
                vertices.append(tuple(x / norm for x in point))
            return midpoints[key]

        refined = []
        for a, b, c in faces:
            ab, bc, ca = midpoint(a, b), midpoint(b, c), midpoint(c, a)
            refined.extend([(a, ab, ca), (ab, b, bc), (ca, bc, c), (ab, bc, ca)])
        faces = refined
    lines = ['0 Closed conducting sphere; coordinates in metres']
    for face in faces:
        lines.append('T sphere ' + ' '.join(f'{vertices[v][i] * radius + translation[i]:.17g}'
                     for v in face for i in range(3)))
    path.write_text('\n'.join(lines) + '\n')
    return len(faces)


def run(args):
    prepared = json.loads(args.preparation.read_text())
    if prepared.get('status') != 'prepared' or prepared.get('version') != '6.0.9':
        raise ValueError('requires pinned field-solver preparation')
    verify_files(prepared['artifact_sha256'])
    hashes = dict(prepared['artifact_sha256'])
    files = [SOURCE, args.preparation.resolve(), SOURCE.parents[1] / 'prepare_fastercap.py',
             SOURCE.parents[1] / 'input_charge.py', ANALOG / 'pdk/1G_repeater_IHPSG13G2/tool_process.py']
    hashes.update({str(p): digest(p) for p in files})
    args.work.mkdir(parents=True, exist_ok=True)
    work = Path(tempfile.mkdtemp(prefix='run-', dir=args.work.resolve()))
    report = dict(status='running', work=str(work), source_sha256=hashes, cases={},
        scope='independent analytical solver/unit/permittivity checks; excludes PDK layer-stack and layout calibration')

    def save():
        text = json.dumps(report, indent=2) + '\n'
        (work / 'checks.json').write_text(text)
        (args.work / 'checks.json').write_text(text)

    save()
    try:
        for name, level, radius, epsilon, translation, limit in (
                ('coarse', 3, 1e-6, 1., (0., 0., 0.), .01),
                ('fine', 4, 1e-6, 1., (0., 0., 0.), .0025),
                ('dielectric', 4, 1e-6, 3.9, (0., 0., 0.), .0025),
                ('scaled_translated', 4, 2e-6, 1., (10e-6, 20e-6, -3e-6), .0025)):
            case = work / name
            case.mkdir()
            panels = sphere(case / 'sphere.txt', level, radius, translation)
            (case / 'sphere.lst').write_text(f'C sphere.txt {epsilon:.17g} 0 0 0\n')
            with (case / 'solver.log').open('w') as log:
                execute(['prlimit', '--as=1073741824', '--', prepared['executable'], '-b',
                    'sphere.lst', '-a0.002', '-g'], cwd=case,
                    env=dict(os.environ, OMP_NUM_THREADS='1'), stdout=log, stderr=-2,
                    check=True, timeout=180, memory_exclusive=False)
            output = (case / 'solver.log').read_text()
            values = re.findall(r'^g1_sphere\s+([-+\deE.]+)\s*$', output, re.M)
            convergence = re.findall(r'Weighted Frobenius norm[^\n]*:\s*([-+\deE.]+)', output)
            if (not values or 'Total time:' not in output or not convergence
                    or float(convergence[-1]) >= .002
                    or re.search(r'^\s*(error|warning)\b|\babort(?:ed|ing)?\b', output, re.I | re.M)):
                raise ValueError('incomplete field solve or diagnostic requiring review')
            capacitance = float(values[-1])
            expected = 4 * math.pi * 8.8541878128e-12 * epsilon * radius
            error = abs(capacitance / expected - 1)
            if not math.isfinite(capacitance) or capacitance <= 0 or error >= limit:
                raise ValueError('field solution differs from independent electrostatic oracle')
            report['cases'][name] = dict(input_panels=panels, radius_m=radius, relative_permittivity=epsilon,
                capacitance_F=capacitance, analytical_capacitance_F=expected, relative_error=error,
                final_relative_matrix_change=float(convergence[-1]),
                artifact_sha256={str(p): digest(p) for p in sorted(case.iterdir()) if p.is_file()})
            save()
        cases = report['cases']
        if cases['fine']['relative_error'] >= .35 * cases['coarse']['relative_error']:
            raise ValueError('sphere surface refinement did not converge')
        for name, scale in [('dielectric', 3.9), ('scaled_translated', 2.)]:
            if abs(cases[name]['capacitance_F'] / cases['fine']['capacitance_F'] / scale - 1) > 1e-4:
                raise ValueError('field solver changed permittivity or geometric scaling')
        verify_files(hashes)
        report['status'] = 'pass'
        print('PASS field solver: sphere, geometric refinement, permittivity and translated size scaling')
    except BaseException as error:
        report.update(status='fail', error=str(error))
        raise
    finally:
        save()


if __name__ == '__main__':
    configure_cleanup()
    parser = argparse.ArgumentParser(description=__doc__)
    for name in ('preparation', 'work'):
        parser.add_argument('--' + name, type=Path, required=True)
    run(parser.parse_args())
