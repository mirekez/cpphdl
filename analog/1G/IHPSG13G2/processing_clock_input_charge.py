#!/usr/bin/env python3
"""Measure the admission pin's PSP charge before budgeting its digital driver."""
import argparse
import gzip
import hashlib
import json
import os
from pathlib import Path
import re
import shutil
import sys
import tempfile

SOURCE = Path(__file__).resolve()
ANALOG = SOURCE.parents[2]
sys.path.insert(0, str(ANALOG / 'pdk/1G_repeater_IHPSG13G2'))
from input_charge import edge_charge, verify_files
from tool_process import execute, configure_cleanup


def digest(path):
    with Path(path).open('rb') as stream:
        return hashlib.file_digest(stream, 'sha256').hexdigest()


def experiment(source, supply, level):
    if supply not in (1.08, 1.2, 1.32) or level not in (0, 1):
        raise ValueError('undeclared admission-input operating point')
    if source.count('.control\n') != 1:
        raise ValueError('ambiguous gate experiment')
    nodes = []
    for index in (0, 1):
        found = re.findall(r'^let observation' + str(index) + r' = v\((n\d+)\)$', source, re.M)
        if len(found) != 1:
            raise ValueError('missing clock/admission observation')
        nodes.append(found[0])
    clock, pin = nodes
    def driver(node):
        matches = re.findall(r'^Rphysical\d+ (n\d+) ' + node + r' 50$', source, re.M)
        if len(matches) != 1:
            raise ValueError('missing or ambiguous 50 ohm input driver')
        return matches[0]
    command, raw = driver(pin), driver(clock)
    deck = source.split('.control\n')[0]
    for node, replacement in ((raw, f'Vraw {raw} 0 PWL(0 0 2n {level * supply})'),
            (command, f'Vadmit {command} 0 PULSE(0 {supply} 10n 100p 100p 4.9n 10n)')):
        deck, count = re.subn(r'^Vsource\d+ ' + node + r' 0 PWL\([^\n]+\)$', replacement, deck, flags=re.M)
        if count != 1:
            raise ValueError('missing or duplicate gate voltage source')
    deck += (f'.save v({command}) v({pin})\n.control\nset num_threads=1\n'
        'set wr_singlescale\nset wr_vecnames\nset numdgt=17\n'
        f'tran 0.5p 90n 0 0.5p uic\nwrdata trace.dat v({command}) v({pin})\nquit\n.endc\n.end\n')
    return deck, ['time', f'v({command})', f'v({pin})']


def run(args):
    proof = json.loads(args.gate_proof.read_text())
    if proof.get('status') != 'pass' or set(proof.get('flows', {})) != {'cpp', 'veriloga', 'psp_tt', 'psp_ss', 'psp_ff'}:
        raise ValueError('requires independently passed admission-gate flows')
    verify_files(proof['source_sha256'])
    args.work.mkdir(parents=True, exist_ok=True)
    work = Path(tempfile.mkdtemp(prefix='run-', dir=args.work.resolve()))
    inputs = [SOURCE, SOURCE.with_name('input_charge.py'), args.gate_proof.resolve(), args.ngspice.resolve(),
              ANALOG / 'pdk/1G_repeater_IHPSG13G2/tool_process.py']
    hashes = dict(proof['source_sha256'], **{str(p): digest(p) for p in inputs})
    report = dict(status='running', work=str(work), source_sha256=hashes, cases=[],
        load_budget_F=80e-15, scope='admission pin charge at 100 ps command edges; excludes interconnect, driver waveform and latch aperture')
    def save():
        text = json.dumps(report, indent=2) + '\n'
        (work / 'checks.json').write_text(text)
        (args.work / 'checks.json').write_text(text)
    save()
    try:
        for flow, supply in (('psp_tt', 1.2), ('psp_ss', 1.08), ('psp_ff', 1.32)):
            original = Path(proof['work']) / flow / 'bench.cir'
            if proof['artifact_sha256'].get(str(original)) != digest(original):
                raise ValueError('qualified gate deck changed')
            for level in (0, 1):
                case = work / (flow + '_' + str(level))
                case.mkdir()
                deck, header = experiment(original.read_text(), supply, level)
                (case / 'bench.cir').write_text(deck)
                if args.reuse:
                    retained = args.reuse / case.name
                    if (retained / 'bench.cir').read_text() != deck:
                        raise ValueError('retained experiment differs from the exact requested deck')
                    shutil.copyfile(retained / 'trace.dat', case / 'trace.dat')
                else:
                    env = dict(os.environ, PDK_ROOT=str(args.pdk.resolve()), PDK='ihp-sg13g2',
                        SPICE_USERINIT_DIR=str(args.pdk.resolve() / 'ihp-sg13g2/libs.tech/ngspice'), OMP_NUM_THREADS='1')
                    with (case / 'ngspice.log').open('w') as log:
                        execute([str(args.ngspice.resolve()), '-b', 'bench.cir'], cwd=case, env=env,
                                stdout=log, stderr=-2, check=True, timeout=600)
                trace = case / 'trace.dat'
                with trace.open() as stream:
                    if next(stream).split() != header:
                        raise ValueError('changed pin-charge observation interface')
                    coverage = []
                    def samples():
                        for line in stream:
                            if line.strip():
                                row = tuple(map(float, line.split()))
                                if len(row) != 3:
                                    raise ValueError('incomplete pin-charge row')
                                if not coverage:
                                    coverage.extend([row[0], row[0]])
                                coverage[1] = row[0]
                                yield row
                    result = edge_charge(samples(), supply, startup=20e-9, maximum_step=.501e-12)
                if not coverage or not 0 <= coverage[0] <= .5e-12 or abs(coverage[1] - 90e-9) > 1e-15:
                    raise ValueError('input-charge transient did not cover the full experiment')
                if result['transitions'] != 14 or result['measured_edges'] != 12:
                    raise ValueError('incomplete admission-charge experiment')
                result.update(flow=flow, raw_clock_level=level, reused=bool(args.reuse),
                    trace_sha256=digest(trace), deck_sha256=digest(case / 'bench.cir'))
                with trace.open('rb') as source, gzip.open(str(trace) + '.gz', 'wb') as target:
                    shutil.copyfileobj(source, target)
                trace.unlink()
                report['cases'].append(result)
                save()
        worst = max(case['absolute_effective_capacitance_F'] for case in report['cases'])
        report.update(maximum_absolute_capacitance_F=worst, remaining_interconnect_budget_F=80e-15 - worst)
        verify_files(hashes)
        if worst > 80e-15:
            raise ValueError('admission pin exceeds the updated digital output budget')
        report['status'] = 'pass'
        print(f'PASS: six PSP input-charge cases, maximum {worst * 1e15:.3f} fF within 80 fF')
    except BaseException as error:
        report.update(status='fail', error=str(error))
        raise
    finally:
        save()


if __name__ == '__main__':
    configure_cleanup()
    parser = argparse.ArgumentParser(description=__doc__)
    for name in ('gate-proof', 'pdk', 'ngspice', 'work'):
        parser.add_argument('--' + name, type=Path, required=True)
    parser.add_argument('--reuse', type=Path, help='recheck retained raw traces only when regenerated decks match exactly')
    run(parser.parse_args())
