"""Check an isolated timing grid against analytic RC states and uniform SPICE."""
import argparse
import hashlib
import json
import math
from pathlib import Path
import sys
import tempfile
import unittest

SOURCE = Path(__file__).resolve()
ANALOG = SOURCE.parents[2]
sys.path[:0] = [str(SOURCE.parents[1]), str(ANALOG / '1G/tests')]
from time_grid import windowed_transient, check_observed_grid
from compare import errors
from hardware import configure_cleanup, execute

WARMUP, STOP, STEP = 16e-6, 16.128e-6, 20e-12
DECK = '''Continuous warmup and measurement states
Vslow input 0 1
Rslow input slow 1000
Cslow slow 0 1n IC=0
Vfast source 0 PWL(0 0 16u 0 16.001u 1 16.128u 1)
Rfast source fast 1000
Cfast fast 0 5p IC=0
.control
set noaskquit
set wr_singlescale
set wr_vecnames
set numdgt=17
save v(slow) v(fast)
tran 2e-11 1.6128e-5 1.6e-5 2e-11 uic
wrdata spice.dat v(slow) v(fast)
quit
.endc
.end
'''


class Structure(unittest.TestCase):
    def test_only_transient_ceiling_and_zero_source_change(self):
        changed, metadata = windowed_transient(DECK, 1e-9)
        original_lines = DECK.splitlines()
        changed_lines = changed.splitlines()
        changed_lines.remove(metadata['source_card'])
        index = next(i for i, line in enumerate(changed_lines) if line.startswith('tran '))
        self.assertEqual(changed_lines[index].split()[1:4], original_lines[index].split()[1:4])
        changed_lines[index] = original_lines[index]
        self.assertEqual(changed_lines, original_lines)
        self.assertIn('PULSE(0 0 ', metadata['source_card'])

    def test_invalid_steps_and_missing_warmup(self):
        for step in (0, -1, math.nan, math.inf, STEP, WARMUP):
            with self.subTest(step=step), self.assertRaises(ValueError):
                windowed_transient(DECK, step)
        with self.assertRaises(ValueError):
            windowed_transient(DECK.replace('1.6e-5', '0'), 1e-9)

    def test_duplicate_analysis_and_source_collision(self):
        for deck in (DECK + DECK, DECK + '* Vsim_timestep_grid already used\n'):
            with self.assertRaises(ValueError):
                windowed_transient(deck, 1e-9)

    def test_sparse_truncated_and_backward_measurements_fail(self):
        _, metadata = windowed_transient(DECK, 1e-9)
        rows = [[WARMUP + i * STEP] for i in range(6401)]
        check_observed_grid(rows, metadata)
        for broken in (rows[::10], rows[:-1], rows[:20] + list(reversed(rows[20:30])) + rows[30:]):
            with self.assertRaises(ValueError):
                check_observed_grid(broken, metadata)


def digest(path):
    return hashlib.sha256(Path(path).read_bytes()).hexdigest()


def read(path):
    rows = [[float(value) for value in line.split()] for line in path.read_text().splitlines()[1:]]
    if any(len(row) != 3 or not all(math.isfinite(value) for value in row) for row in rows):
        raise ValueError('invalid RC trajectory')
    return rows


def analytic(rows):
    peak = 0
    for time, slow, fast in rows:
        elapsed = max(0, time - WARMUP)
        rise, tau = 1e-9, 5e-9
        if elapsed <= rise:
            expected_fast = elapsed / rise + tau / rise * math.expm1(-elapsed / tau)
        else:
            expected_fast = 1 + tau / rise * math.expm1(-rise / tau) * math.exp(-(elapsed - rise) / tau)
        peak = max(peak, abs(slow - (1 - math.exp(-time / 1e-6))), abs(fast - expected_fast))
    if peak > .00005:
        raise ValueError('RC absolute error exceeds 50 uV')
    return peak


def run(args):
    root = args.work.resolve()
    root.mkdir(parents=True, exist_ok=True)
    work = Path(tempfile.mkdtemp(prefix='run-', dir=root))
    hashes = {str(path): digest(path) for path in (SOURCE, SOURCE.parents[1] / 'time_grid.py',
        SOURCE.parents[1] / 'compare.py', args.ngspice.resolve())}
    report = dict(status='running', source_sha256=hashes, run_directory=str(work))
    try:
        windowed, metadata = windowed_transient(DECK, 1e-9)
        waves = {}
        for name, deck in (('uniform', DECK), ('windowed', windowed)):
            directory = work / name
            directory.mkdir()
            (directory / 'bench.cir').write_text(deck)
            with (directory / 'ngspice.log').open('w') as log:
                execute([str(args.ngspice.resolve()), '-n', '-b', 'bench.cir'], cwd=directory,
                    stdout=log, stderr=-2, check=True, timeout=120)
            waves[name] = read(directory / 'spice.dat')
        report['grid'] = check_observed_grid(waves['windowed'], metadata)
        report['absolute_error_V'] = {name: analytic(rows) for name, rows in waves.items()}
        # Keep every observation in absolute checks. Remove only repeated-time
        # interpolation knots; compare all original states in both directions.
        unique = {name: [row for i, row in enumerate(rows) if i == 0 or row[0] != rows[i - 1][0]]
                  for name, rows in waves.items()}
        difference = [max(a, b) for a, b in zip(errors(waves['uniform'], unique['windowed']),
                                               errors(waves['windowed'], unique['uniform']))]
        report['peak_differences_V'] = difference
        if max(difference) > .00005:
            raise ValueError('continuous-state uniform/grid comparison exceeds 50 uV')
        report['peak_differences_V'] = difference
        if any(digest(path) != value for path, value in hashes.items()):
            raise ValueError('timing-grid test inputs changed')
        report['artifacts'] = {str(path): digest(path) for path in work.rglob('*')
                               if path.is_file() and path.suffix in ('.cir', '.dat')}
        report['status'] = 'pass'
        print('PASS continuous RC states, uniform/grid comparison and measurement-step bound')
    except BaseException as error:
        report.update(status='fail', error=str(error))
        raise
    finally:
        payload = json.dumps(report, indent=2) + '\n'
        (root / 'checks.json').write_text(payload)
        (work / 'checks.json').write_text(payload)


if __name__ == '__main__':
    configure_cleanup()
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--ngspice', type=Path, required=True)
    parser.add_argument('--work', type=Path, required=True)
    args = parser.parse_args()
    if not unittest.TextTestRunner().run(unittest.defaultTestLoader.loadTestsFromTestCase(Structure)).wasSuccessful():
        raise SystemExit(1)
    run(args)
