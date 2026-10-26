#!/usr/bin/env python3
"""Check corrected disk-block multiplication against electrostatics and RAM solves."""
import argparse
import json
import math
import os
from pathlib import Path
import re
import sys
import tempfile

SOURCE = Path(__file__).resolve()
sys.path[:0] = [str(SOURCE.parent), str(SOURCE.parents[1])]
from field_solver import sphere, ANALOG
from prepare_fastercap import digest
from input_charge import verify_files
from tool_process import execute, configure_cleanup


def run(args):
    prepared = json.loads(args.preparation.read_text())
    if prepared.get('status') != 'prepared' or prepared.get('local_corrections') != ['per-block-matrix-offset']:
        raise ValueError('requires the reviewed per-block matrix-offset build')
    verify_files(prepared['artifact_sha256'])
    verify_files(prepared['source_sha256'])
    hashes = {**prepared['artifact_sha256'], **prepared['source_sha256']}
    files = [SOURCE, SOURCE.with_name('field_solver.py'), args.preparation.resolve(),
        SOURCE.parents[1] / 'prepare_fastercap.py', SOURCE.parents[1] / 'input_charge.py',
        ANALOG / 'pdk/1G_repeater_IHPSG13G2/tool_process.py']
    hashes.update({str(p): digest(p) for p in files})
    args.work.mkdir(parents=True, exist_ok=True)
    work = Path(tempfile.mkdtemp(prefix='run-', dir=args.work.resolve()))
    report = dict(status='running', work=str(work), source_sha256=hashes, cases={},
        preparation_sha256=digest(args.preparation),
        scope='independent sphere capacitance and RAM/disk equality for one and two conductors; excludes PDK calibration')

    def save():
        text = json.dumps(report, indent=2) + '\n'
        (work / 'checks.json').write_text(text)
        (args.work / 'checks.json').write_text(text)

    save()
    try:
        for count in (1, 2):
            for mode, threshold in (('memory', 2), ('disk', 100)):
                name = f'{count}_{mode}'
                case = work / name
                case.mkdir()
                sphere(case / 'sphere.txt', 5, 1e-6)
                geometry = 'C sphere.txt 1 0 0 0\n'
                if count == 2:
                    geometry += 'C sphere.txt 1 4e-6 0 0\n'
                (case / 'geometry.lst').write_text(geometry)
                with (case / 'solver.log').open('w') as log:
                    execute(['prlimit', '--as=1073741824', '--', prepared['executable'], '-b', '-g',
                        '-m1000', '-f' + str(threshold), 'geometry.lst'], cwd=case,
                        env=dict(os.environ, OMP_NUM_THREADS='1', TMPDIR=str(case)), stdout=log, stderr=-2,
                        check=True, timeout=600, memory_exclusive=False)
                output = (case / 'solver.log').read_text()
                disk = 'Going out-of-core.' in output
                if (disk != (mode == 'disk') or 'Total time:' not in output
                        or re.search(r'^\s*(?:error|warning)\b', output, re.I | re.M)):
                    raise ValueError('unreviewed diagnostic or requested memory mode was not exercised')
                matches = re.findall(r'^g\d+_sphere\s+([^\n]+)$', output, re.M)
                matrix = [[float(v) for v in row.split()] for row in matches]
                if len(matrix) != count or any(len(row) != count for row in matrix):
                    raise ValueError('changed fixed-mesh conductor matrix')
                for i in range(count):
                    if (not all(math.isfinite(v) for v in matrix[i]) or matrix[i][i] <= 0
                            or sum(matrix[i]) < 0 or any(matrix[i][j] >= 0 for j in range(count) if i != j)):
                        raise ValueError('nonphysical fixed-mesh Maxwell matrix')
                if count == 1:
                    expected = 4 * math.pi * 8.8541878128e-12 * 1e-6
                    if abs(matrix[0][0] / expected - 1) >= .0025:
                        raise ValueError('sphere does not match independent analytical capacitance')
                else:
                    if (abs(matrix[0][1] / matrix[1][0] - 1) > .005
                            or abs(matrix[0][0] / matrix[1][1] - 1) > .005):
                        raise ValueError('symmetric two-sphere geometry lost reciprocity or symmetry')
                report['cases'][name] = dict(matrix_F=matrix, out_of_core=disk,
                    artifact_sha256={str(p): digest(p) for p in sorted(case.iterdir()) if p.is_file()})
                save()
            memory = report['cases'][f'{count}_memory']['matrix_F']
            disk = report['cases'][f'{count}_disk']['matrix_F']
            if any(abs(disk[i][j] / memory[i][j] - 1) > 1e-5 for i in range(count) for j in range(count)):
                raise ValueError('disk-backed multiplication differs from in-memory solution')
        verify_files(hashes)
        report.update(status='pass', disk_mode_qualified=True)
        print('PASS corrected field solver: analytical sphere and RAM/disk matrices for one/two conductors')
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
