#!/usr/bin/env python3
"""Check sidewall coefficients independently of field-mesh convergence.

Isolated copies of the PDK set only sidewall offsets to zero, making each
expected mutual capacitance exactly coefficient * facing length / gap.
The production PDK and its empirical offsets remain unchanged.
"""
import argparse
import hashlib
import json
from pathlib import Path
import re
import shutil
import subprocess
import sys
import tempfile

import klayout.db as db

SOURCE = Path(__file__).resolve()
ANALOG = SOURCE.parents[3]
sys.path.insert(0, str(SOURCE.parents[1]))
from extract_analog import number


def digest(path):
    return hashlib.sha256(Path(path).read_bytes()).hexdigest()


def fixture(path, coefficients):
    layout = db.Layout()
    layout.dbu = .001
    top = layout.create_cell('SidewallOracle')
    expected = {}
    for metal, layer in enumerate((8, 10, 30, 50, 67, 126, 134), 1):
        for index, (name, length, gap, vertical, reverse, shield) in enumerate([
                ('horizontal', 20., 2., False, False, False),
                ('vertical', 20., 2., True, False, False),
                ('reflected', 40., 3.2, False, True, False),
                ('shield', 20., 2., False, False, True)]):
            labels = []
            for wire in range(3 if shield else 2):
                label = f'm{metal}_{name}_{chr(65 + wire)}'
                labels.append(label)
                bottom = wire * (2 + gap)
                box = db.DBox(0, bottom, length, bottom + 2)
                trans = db.DTrans(1 if vertical else 0,
                                  reverse, index * 200 + 100, metal * 100)
                box = box.transformed(trans)
                for datatype in (0, 2):
                    top.shapes(layout.layer(layer, datatype)).insert(box)
                top.shapes(layout.layer(layer, 25)).insert(db.DText(label,
                    db.DTrans(box.center().x, box.center().y)))
            cap = coefficients[metal] * 1e-18 * length / gap
            expected[tuple(sorted(labels[:2]))] = cap
            if shield:
                expected[tuple(sorted(labels[1:]))] = cap
                expected[tuple(sorted((labels[0], labels[2])))] = 0.
    layout.write(str(path))
    return expected


def run(args):
    args.work.mkdir(parents=True, exist_ok=True)
    work = Path(tempfile.mkdtemp(prefix='run-', dir=args.work.resolve()))
    technology = args.pdk.resolve() / 'ihp-sg13g2/libs.tech/magic'
    extraction = (technology / 'ihp-sg13g2-extract.tech').read_text()
    pattern = r'(?m)^\s*defaultsidewall\s+allm([1-7])\s+metal[1-7]\s+([-+\d.]+)\s+([-+\d.]+)\s*$'
    coefficients = {}
    for match in re.finditer(pattern, extraction):
        coefficients.setdefault(int(match[1]), float(match[2]))
    if set(coefficients) != set(range(1, 8)):
        raise ValueError('missing metal sidewall coefficients')
    tracked = [SOURCE, SOURCE.parents[1] / 'extract_analog.py', args.magic.resolve(),
               Path(sys.executable).resolve(), *[p for p in technology.rglob('*') if p.is_file()]]
    runtime = args.magic.resolve().parents[1] / 'lib/magic/tcl'
    if not (runtime / 'tclmagic.so').is_file():
        raise ValueError('requires a prepared local Magic runtime')
    tracked += [p for p in runtime.rglob('*') if p.is_file()]
    hashes = {str(p): digest(p) for p in tracked}
    report = dict(status='running', work=str(work), source_sha256=hashes, cases={},
        scope='sidewall parser coefficients, orientation, length/gap scaling and same-plane shielding; '
              'zero-offset fixture technology only, not field calibration or chip qualification')

    def save():
        text = json.dumps(report, indent=2) + '\n'
        (work / 'checks.json').write_text(text)
        (args.work / 'checks.json').write_text(text)

    save()
    try:
        gds = work / 'wires.gds'
        expected = fixture(gds, coefficients)
        for keyword in ('defaultsidewall', 'sidewall'):
            case = work / keyword
            case.mkdir()
            tech = case / 'technology'
            shutil.copytree(technology, tech)

            def zero_offset(match):
                metal, coefficient = match[1], match[2]
                return (f' defaultsidewall allm{metal} metal{metal} {coefficient} 0' if
                    keyword == 'defaultsidewall' else
                    f' sidewall allm{metal} space space allm{metal} {coefficient} 0')

            (tech / 'ihp-sg13g2-extract.tech').write_text(re.sub(pattern, zero_offset, extraction))
            commands = ['drc off', 'gds read {' + str(gds) + '}', 'load SidewallOracle',
                'select top cell', 'expand', 'extract style ngspice', 'extract all',
                'ext2spice format ngspice', 'ext2spice scale off', 'ext2spice hierarchy off',
                'ext2spice subcircuit top on', 'ext2spice merge none', 'ext2spice cthresh 0',
                'ext2spice -o wires.spice']
            (case / 'extract.tcl').write_text('if {[catch {\n' + '\n'.join(commands) +
                '\n} message]} { puts stderr $message; exit 1 }\nquit -noprompt\n')
            with (case / 'extract.log').open('w') as log:
                subprocess.run([str(args.magic.resolve()), '-dnull', '-noconsole', '-rcfile',
                    str(tech / 'ihp-sg13g2.magicrc'), 'extract.tcl'], cwd=case,
                    stdout=log, stderr=-2, check=True, timeout=120)
            if re.search(r'\b(?:error|unrecognized|unknown|failed)\b',
                         (case / 'extract.log').read_text(), re.I):
                raise ValueError('unreviewed sidewall extraction diagnostic')
            measured = {}
            for a, b, value in re.findall(r'^C\S+\s+(\S+)\s+(\S+)\s+(\S+)\s*$',
                                         (case / 'wires.spice').read_text(), re.M):
                pair = tuple(sorted((a, b)))
                measured[pair] = measured.get(pair, 0.) + number(value)
            checks = []
            for pair, cap in expected.items():
                actual = measured.get(pair, 0.)
                # ext2spice prints fF values to five decimal places. Allow
                # one half of the final printed digit, plus float roundoff.
                limit = max(5.1e-21 if cap else 1e-22, cap * 1e-5)
                checks.append(dict(nets=pair, expected_F=cap, actual_F=actual,
                    pass_limit_F=limit, passed=abs(actual - cap) <= limit))
            # No coupling may appear between physically distant fixtures.
            unexpected = [pair for pair in measured if pair not in expected and
                          all(n.startswith('m') for n in pair)]
            report['cases'][keyword] = dict(checks=checks, unexpected_pairs=unexpected,
                passed=all(c['passed'] for c in checks) and not unexpected)
            save()
        report['artifact_sha256'] = {str(p): digest(p) for p in work.rglob('*')
                                    if p.is_file() and p.name != 'checks.json'}
        if any(digest(p) != value for p, value in hashes.items()):
            raise ValueError('sidewall oracle inputs changed during extraction')
        if not all(c['passed'] for c in report['cases'].values()):
            raise ValueError('extracted sidewall coefficients or shielding failed the analytical oracle')
        report['status'] = 'pass'
        print('PASS 84 sidewall checks: seven metals, two parser keywords, rotation/reflection, scaling and shielding')
    except BaseException as error:
        report.update(status='fail', error=str(error))
        raise
    finally:
        save()


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    for name in ('pdk', 'magic', 'work'):
        parser.add_argument('--' + name, type=Path, required=True)
    run(parser.parse_args())
