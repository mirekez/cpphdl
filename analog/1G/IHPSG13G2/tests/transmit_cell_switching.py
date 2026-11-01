#!/usr/bin/env python3
"""Check extracted current-cell steering at an 8 ns symbol interval."""
import argparse
import json
import math
import os
from pathlib import Path
import sys
import tempfile

SOURCE = Path(__file__).resolve()
sys.path[:0] = [str(SOURCE.parents[1]), str(SOURCE.parents[3] / 'pdk/1G_repeater_IHPSG13G2')]
from extract_analog import digest
from tool_process import configure_cleanup, execute


def measure(rows, current):
    if (len(rows) < 2000 or any(len(r) != 5 or not all(math.isfinite(v) for v in r) for r in rows)
            or rows[0][0] > 1e-12 or abs(rows[-1][0] - 42e-9) > 1e-12
            or any(b[0] <= a[0] or b[0] - a[0] > 20.001e-12 for a, b in zip(rows, rows[1:]))):
        raise ValueError('incomplete or nonfinite switching trajectory')
    windows = []
    for index in range(5):
        # Complementary commands change at 2, 10, 18, 26 and 34 ns.
        # Check the last two ns of every symbol, ending before its next edge.
        start, stop = (8 + 8 * index) * 1e-9, (9.9 + 8 * index) * 1e-9
        selected = [r for r in rows if start <= r[0] <= stop]
        if len(selected) < 90:
            raise ValueError('missing settled-symbol observations')
        sign = 1 if index % 2 == 0 else -1
        bounds = {}
        for flow, offset, limit in (('extracted', 1, .05), ('reference', 3, .01)):
            differential = max(abs((r[offset] - r[offset + 1]) / current - sign) for r in selected)
            total = max(abs((r[offset] + r[offset + 1]) / current - 1) for r in selected)
            if max(differential, total) > limit:
                raise ValueError(flow + ': switched current fails its independent plateau limit')
            bounds[flow] = dict(differential_relative_error=differential,
                                total_relative_error=total, relative_error_limit=limit)
        difference = max(abs((r[1] - r[2]) - (r[3] - r[4])) / current for r in selected)
        if difference > .05:
            raise ValueError('extracted/reference switched-current difference exceeds 5%')
        windows.append(dict(start_s=start, stop_s=stop, sign=sign, samples=len(selected),
                            comparison_relative_error=difference, **bounds))
    return dict(symbol_interval_s=8e-9, observed_symbols=5, windows=windows,
        maximum_relative_difference=max(w['comparison_relative_error'] for w in windows))


def run(args):
    evidence = json.loads(args.proof.read_text())
    if evidence.get('status') != 'pass' or set(evidence.get('cases', {})) != {'main', 'history'}:
        raise ValueError('complete connected current-cell geometry/DC proof required')
    hashes = dict(evidence['source_sha256'])
    models = args.pdk.resolve() / 'ihp-sg13g2/libs.tech/ngspice'
    paths = [SOURCE, args.proof.resolve(), args.ngspice.resolve(), models / '.spiceinit',
             SOURCE.parents[1] / 'extract_analog.py',
             SOURCE.parents[3] / 'pdk/1G_repeater_IHPSG13G2/tool_process.py']
    paths += [p for p in models.rglob('*') if p.is_file() and p.suffix in ('.lib', '.spice', '.osdi')]
    for case, record in evidence['cases'].items():
        extraction = record['extraction']
        if extraction.get('status') != 'pass':
            raise ValueError('failed current-cell extraction')
        for group in (record['artifact_sha256'], extraction['source_sha256'], extraction['artifact_sha256']):
            for path, value in group.items():
                if path in hashes and hashes[path] != value:
                    raise ValueError('inconsistent current-cell evidence')
                hashes[path] = value
    hashes.update({str(p): digest(p) for p in paths})
    if any(digest(p) != value for p, value in hashes.items()):
        raise ValueError('stale current-cell evidence')
    args.work.mkdir(parents=True, exist_ok=True)
    work = Path(tempfile.mkdtemp(prefix='run-', dir=args.work.resolve()))
    report = dict(status='running', work=str(work), source_sha256=hashes, cases={},
        scope='TT27C, ideal complementary gate and bias sources; extracted RC, independent plateau/current-sum and comparison checks; excludes actual gate driver, startup bias, line loading, PVT and full transmitter')
    env = dict(os.environ, PDK_ROOT=str(args.pdk.resolve()), PDK='ihp-sg13g2',
               SPICE_USERINIT_DIR=str(models), OMP_NUM_THREADS='1')

    def save():
        payload = json.dumps(report, indent=2) + '\n'
        (work / 'checks.json').write_text(payload)
        (args.work / 'checks.json').write_text(payload)

    try:
        save()
        for case in ('main', 'history'):
            current = .0075 if case == 'main' else .0025
            widths = (400, 1500, 450) if case == 'main' else (200, 500, 150)
            source = Path(evidence['work']) / case / 'extraction/conserved.spice'
            if str(source) not in hashes:
                raise ValueError('missing extracted circuit identity')
            for voltage in (1.3, 2.3, 3.3):
                name = f'{case}-{voltage:g}'
                target = work / name
                target.mkdir()
                points = evidence['cases'][case]['dc'][f'P-{voltage:g}']['targets']
                if len(points) != 3 or not math.isclose(points[1]['reference_current_A'], current):
                    raise ValueError('changed nominal branch-current contract')
                bias = points[1]['gate_V']
                deck = ('* Actual connected current-cell switching against independent PSP reference\n'
                    f'.lib "{models / "models/cornerMOShv.lib"}" mos_tt\n' + source.read_text() + '\n'
                    f'VEP EP 0 {voltage}\nVEN EN 0 {voltage}\nVRP RP 0 {voltage}\nVRN RN 0 {voltage}\n'
                    'VGP GP 0 PULSE(0 3.3 2n .1n .1n 7.9n 16n)\n'
                    'VGN GN 0 PULSE(3.3 0 2n .1n .1n 7.9n 16n)\n'
                    f'VBIAS BIAS 0 {bias:.17g}\nVCAS CAS 0 1.8\n'
                    'Xactual EP EN GP GN BIAS CAS 0 ExtractedAnalog\n')
                for index, (width, nodes) in enumerate(((widths[0], 'RP GP TAIL 0'),
                        (widths[0], 'RN GN TAIL 0'), (widths[1], 'TAIL CAS LOWER 0'),
                        (widths[2], 'LOWER BIAS 0 0'))):
                    for finger in range(width // 5):
                        deck += f'Xref{index}_{finger} {nodes} sg13_hv_nmos w=5u l=.45u\n'
                deck += ('.temp 27\n.options klu\n.control\nset wr_singlescale\nset wr_vecnames\n'
                    'set numdgt=17\ntran 10p 42n 0 20p\n'
                    'let ep_current = -i(VEP)\nlet en_current = -i(VEN)\n'
                    'let rp_current = -i(VRP)\nlet rn_current = -i(VRN)\n'
                    'wrdata switching.dat ep_current en_current rp_current rn_current\nquit\n.endc\n.end\n')
                (target / 'bench.cir').write_text(deck)
                with (target / 'ngspice.log').open('w') as log:
                    execute(['prlimit', '--as=1073741824', '--', str(args.ngspice.resolve()), '-b', 'bench.cir'],
                        cwd=target, env=env, stdout=log, stderr=-2, check=True, timeout=600, memory_exclusive=False)
                rows = [[float(v) for v in line.split()]
                        for line in (target / 'switching.dat').read_text().splitlines()[1:]]
                report['cases'][name] = measure(rows, current)
                report['cases'][name]['artifact_sha256'] = {str(p): digest(p) for p in
                    (target / 'bench.cir', target / 'ngspice.log', target / 'switching.dat')}
                save()
        if any(digest(p) != value for p, value in hashes.items()):
            raise ValueError('switching qualification input changed')
        report['status'] = 'pass'
        print('PASS six extracted main/history switching cases at an 8 ns symbol interval')
    except BaseException as error:
        report.update(status='fail', error=str(error))
        raise
    finally:
        save()


if __name__ == '__main__':
    configure_cleanup()
    parser = argparse.ArgumentParser(description=__doc__)
    for name in ('proof', 'pdk', 'ngspice', 'work'):
        parser.add_argument('--' + name, type=Path, required=True)
    run(parser.parse_args())
