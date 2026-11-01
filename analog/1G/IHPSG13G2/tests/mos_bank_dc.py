#!/usr/bin/env python3
"""Bound nominal transmitter-bank DC current loss from actual extracted wiring."""
import argparse
import json
import math
import os
from pathlib import Path
import sys
import tempfile

SOURCE = Path(__file__).resolve()
sys.path.insert(0, str(SOURCE.parents[1]))
from extract_analog import digest
sys.path.insert(0, str(SOURCE.parents[3] / 'pdk/1G_repeater_IHPSG13G2'))
from tool_process import configure_cleanup, execute

WIDTHS = dict(tx_steering=400, tx_sink=450, tx_cascode=1500,
              tx_history_cascode=500, tx_history_steering=200, tx_history_sink=150)


def measure(path, nominal_current):
    rows = [[float(value) for value in line.split()]
            for line in Path(path).read_text().splitlines()[1:]]
    if (len(rows) != 61 or any(len(row) != 4 or not all(math.isfinite(v) for v in row)
                              for row in rows) or
            any(abs(row[0] - (.6 + index * .01)) > 1e-10 or abs(row[0] - row[1]) > 1e-10
                for index, row in enumerate(rows)) or
            any(b[2] <= a[2] or b[3] <= a[3] for a, b in zip(rows, rows[1:]))):
        raise ValueError('incomplete, nonfinite or nonmonotonic DC sweep')
    targets = []
    for current in (nominal_current / 2, nominal_current, nominal_current * 4 / 3):
        intervals = [(a, b) for a, b in zip(rows, rows[1:]) if a[3] <= current < b[3]]
        if len(intervals) != 1:
            raise ValueError('DC sweep does not bracket the required branch current')
        a, b = intervals[0]
        fraction = (current - a[3]) / (b[3] - a[3])
        actual = a[2] + fraction * (b[2] - a[2])
        targets.append(dict(reference_current_A=current, extracted_current_A=actual,
            relative_error=actual / current - 1, gate_V=a[1] + fraction * (b[1] - a[1])))
    return dict(targets=targets, maximum_relative_error=max(abs(t['relative_error']) for t in targets),
                relative_error_limit=.05)


def run(args):
    evidence = json.loads(args.proof.read_text())
    if evidence.get('status') != 'pass' or set(evidence.get('cases', {})) != set(WIDTHS):
        raise ValueError('complete current transmitter extraction proof required')
    hashes = dict(evidence['source_sha256'])
    if not hashes:
        raise ValueError('extraction proof lacks input hashes')
    models = args.pdk.resolve() / 'ihp-sg13g2/libs.tech/ngspice/models'
    paths = [SOURCE, args.proof.resolve(), args.ngspice.resolve(),
             models.parent / '.spiceinit', SOURCE.parents[1] / 'extract_analog.py',
             SOURCE.parents[3] / 'pdk/1G_repeater_IHPSG13G2/tool_process.py']
    paths += [p for p in models.parent.rglob('*') if p.is_file() and p.suffix in ('.spice', '.lib', '.osdi')]
    extracts = {}
    for case in WIDTHS:
        proof = Path(evidence['cases'][case]['extraction'])
        extraction = json.loads(proof.read_text())
        if extraction.get('status') != 'pass':
            raise ValueError('failed case extraction')
        hashes.update(extraction['source_sha256'])
        hashes.update(extraction['artifact_sha256'])
        paths.append(proof)
        netlist = proof.parent / 'conserved.spice'
        if str(netlist.resolve()) not in extraction['artifact_sha256']:
            raise ValueError('extracted circuit is not a qualified artifact')
        extracts[case] = netlist
    hashes.update({str(p): digest(p) for p in paths})
    if any(digest(path) != value for path, value in hashes.items()):
        raise ValueError('transmitter geometry or extraction evidence is stale')
    args.work.mkdir(parents=True, exist_ok=True)
    work = Path(tempfile.mkdtemp(prefix='run-', dir=args.work.resolve()))
    report = dict(status='running', work=str(work), source_sha256=hashes, cases={},
        scope='TT 27 C, 1 V drain, grounded source/body DC current comparison; excludes connected driver, PVT, dynamic settling and EM')
    environment = dict(os.environ, PDK_ROOT=str(args.pdk.resolve()), PDK='ihp-sg13g2',
                       SPICE_USERINIT_DIR=str(models.parent), OMP_NUM_THREADS='1')
    try:
        for case, width in WIDTHS.items():
            target = work / case
            target.mkdir()
            deck = '* Extracted transmitter bank and independent equal-width PSP reference\n'
            deck += f'.lib "{models / "cornerMOShv.lib"}" mos_tt\n'
            deck += extracts[case].read_text() + '\n'
            deck += 'VPEX D 0 1\nVREF DREF 0 1\nVG G 0 0\nXPEX D G 0 0 ExtractedAnalog\n'
            for index in range(width // 5):
                deck += f'XREF{index} DREF G 0 0 sg13_hv_nmos w=5u l=.45u\n'
            deck += ('.temp 27\n.control\nset wr_singlescale\nset wr_vecnames\nset numdgt=15\n'
                     'dc VG .6 1.2 .01\nlet extracted_current = -i(VPEX)\n'
                     'let reference_current = -i(VREF)\n'
                     'wrdata iv.dat v(G) extracted_current reference_current\nquit\n.endc\n.end\n')
            (target / 'bench.cir').write_text(deck)
            with (target / 'ngspice.log').open('w') as log:
                execute(['prlimit', '--as=1073741824', '--', str(args.ngspice.resolve()), '-b', 'bench.cir'],
                        cwd=target, env=environment, stdout=log, stderr=-2, check=True,
                        timeout=180, memory_exclusive=False)
            measured = measure(target / 'iv.dat', .0025 if 'history' in case else .0075)
            report['cases'][case] = dict(**measured, artifact_sha256={str(p): digest(p)
                for p in (target / 'bench.cir', target / 'ngspice.log', target / 'iv.dat')})
            if measured['maximum_relative_error'] > measured['relative_error_limit']:
                raise ValueError(case + ': extracted current differs by more than 5%')
        if any(digest(path) != value for path, value in hashes.items()):
            raise ValueError('DC qualification inputs changed during simulation')
        report['status'] = 'pass'
        print('PASS six extracted TX banks: nominal DC current error at half, nominal and 4/3 branch current <= 5%')
    except BaseException as error:
        report.update(status='fail', error=str(error))
        raise
    finally:
        payload = json.dumps(report, indent=2) + '\n'
        (work / 'checks.json').write_text(payload)
        (args.work / 'checks.json').write_text(payload)


if __name__ == '__main__':
    configure_cleanup()
    parser = argparse.ArgumentParser(description=__doc__)
    for name in ('proof', 'pdk', 'ngspice', 'work'):
        parser.add_argument('--' + name, type=Path, required=True)
    run(parser.parse_args())
