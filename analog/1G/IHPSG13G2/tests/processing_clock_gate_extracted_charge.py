#!/usr/bin/env python3
"""Check the controller load budget against charge at the extracted gate pin."""
import argparse
import gzip
import json
import os
from pathlib import Path
import shutil
import sys
import tempfile

SOURCE = Path(__file__).resolve()
ANALOG = SOURCE.parents[3]
sys.path[:0] = [str(SOURCE.parents[1]), str(ANALOG / 'pdk/1G_repeater_IHPSG13G2')]
from input_charge import edge_charge, verify_files
from extract_analog import digest
from tool_process import execute, configure_cleanup

CONDITIONS = {'mos_tt': (27, 1.2, 'cap_typ'), 'mos_ss': (125, 1.08, 'cap_wcs'),
              'mos_ff': (-40, 1.32, 'cap_bcs')}


def deck(extracted, models, corner, level):
    temperature, supply, cap = CONDITIONS[corner]
    if level not in (0, 1):
        raise ValueError('raw clock must be held low or high')
    # Charge is measured at the real external ENABLE pin. All extracted wire
    # resistance/coupling, MOS charge and physical MIM elements remain present.
    return f'''Extracted admission pin charge, {corner}, clock held {level}
.lib "{models}/cornerMOSlv.lib" {corner}
.lib "{models}/cornerCAP.lib" {cap}
.temp {temperature}
.include "{extracted}"
Vsupply SUPPLY 0 PWL(0 0 2n {supply})
Rsupply SUPPLY VDD 1
Csupply VDD 0 1p IC=0
Vraw RAW 0 PWL(0 0 2n {level * supply})
Rraw RAW CLOCK 50
Vadmit COMMAND 0 PULSE(0 {supply} 10n 100p 100p 4.9n 10n)
Radmit COMMAND ENABLE 50
Xgate VDD 0 CLOCK ENABLE OUT ExtractedAnalog
Cload OUT 0 200f IC=0
.save v(COMMAND) v(ENABLE)
.options klu method=gear maxord=2 reltol=1e-5 abstol=1e-12 vntol=1e-8 chgtol=1e-16
.control
set num_threads=1
set noaskquit
set wr_singlescale
set wr_vecnames
set numdgt=17
tran 0.5p 90n 0 0.5p uic
wrdata trace.dat v(COMMAND) v(ENABLE)
quit
.endc
.end
'''


def run(args):
    extraction = args.extraction.resolve()
    proof_path = extraction / 'extraction.json'
    proof = json.loads(proof_path.read_text())
    if proof.get('status') != 'pass' or proof.get('ports') != ['VDD', 'VSS', 'CLOCK', 'ENABLE', 'OUT']:
        raise ValueError('requires conserved clock-gate extraction')
    verify_files(proof['source_sha256'])
    verify_files(proof['artifact_sha256'])
    models = args.pdk.resolve() / 'ihp-sg13g2/libs.tech/ngspice/models'
    files = [SOURCE, SOURCE.parents[1] / 'input_charge.py', SOURCE.parents[1] / 'extract_analog.py',
        ANALOG / 'pdk/1G_repeater_IHPSG13G2/tool_process.py', proof_path, args.ngspice.resolve()]
    files += list(models.rglob('*.lib')) + list(models.rglob('*.spice'))
    hashes = {str(p): digest(p) for p in files}
    hashes.update(proof['source_sha256'])
    hashes.update(proof['artifact_sha256'])
    args.work.mkdir(parents=True, exist_ok=True)
    work = Path(tempfile.mkdtemp(prefix='run-', dir=args.work.resolve()))
    report = dict(status='running', work=str(work), source_sha256=hashes, cases=[], load_budget_F=80e-15,
        scope='extracted gate ENABLE charge at 100 ps command edges, both clock states, TT/SS/FF; '
              'includes local gate RC, excludes controller-to-gate interconnect and independent RC calibration')

    def save():
        text = json.dumps(report, indent=2) + '\n'
        (work / 'checks.json').write_text(text)
        (args.work / 'checks.json').write_text(text)

    save()
    try:
        for corner, (_, supply, _) in CONDITIONS.items():
            for level in (0, 1):
                case = work / f'{corner}_{level}'
                case.mkdir()
                (case / 'bench.cir').write_text(deck(extraction / 'conserved.spice', models, corner, level))
                environment = dict(os.environ, OMP_NUM_THREADS='1', PDK_ROOT=str(args.pdk.resolve()),
                    PDK='ihp-sg13g2', SPICE_USERINIT_DIR=str(models.parent))
                retained = args.reuse / case.name if args.reuse else None
                reused = bool(retained and (retained / 'trace.dat').is_file())
                if reused:
                    if (retained / 'bench.cir').read_bytes() != (case / 'bench.cir').read_bytes():
                        raise ValueError('retained charge stimulus differs from the requested deck')
                    shutil.copyfile(retained / 'trace.dat', case / 'trace.dat')
                else:
                    with (case / 'ngspice.log').open('w') as log:
                        execute([str(args.ngspice.resolve()), '-b', 'bench.cir'], cwd=case, env=environment,
                            stdout=log, stderr=-2, timeout=1200, check=True, memory_exclusive=False)
                trace = case / 'trace.dat'
                coverage = []
                with trace.open() as stream:
                    if next(stream).lower().split() != ['time', 'v(command)', 'v(enable)']:
                        raise ValueError('changed extracted charge observation interface')

                    def samples():
                        for line in stream:
                            if line.strip():
                                row = tuple(map(float, line.split()))
                                if len(row) != 3:
                                    raise ValueError('incomplete extracted charge row')
                                if not coverage:
                                    coverage.extend([row[0], row[0]])
                                coverage[1] = row[0]
                                yield row

                    metrics = edge_charge(samples(), supply, startup=20e-9, maximum_step=.501e-12)
                if not coverage or not 0 <= coverage[0] <= .5e-12 or abs(coverage[1] - 90e-9) > 1e-15:
                    raise ValueError('incomplete extracted charge transient')
                if metrics['transitions'] != 14 or metrics['measured_edges'] != 12:
                    raise ValueError('incomplete extracted charge transition coverage')
                with trace.open('rb') as source, gzip.open(str(trace) + '.gz', 'wb') as target:
                    shutil.copyfileobj(source, target)
                metrics.update(corner=corner, raw_clock_level=level, reused=reused, artifact_sha256={
                    str(case / 'bench.cir'): digest(case / 'bench.cir'),
                    str(trace) + '.gz': digest(str(trace) + '.gz')})
                trace.unlink()
                report['cases'].append(metrics)
                save()
        worst = max(case['absolute_effective_capacitance_F'] for case in report['cases'])
        report.update(maximum_absolute_capacitance_F=worst, remaining_external_interconnect_budget_F=80e-15 - worst)
        verify_files(hashes)
        if worst > report['load_budget_F']:
            raise ValueError('extracted admission pin exceeds controller 80 fF load budget')
        report['status'] = 'pass'
        print(f'PASS: six extracted charge cases, worst {worst * 1e15:.3f} fF / 80 fF')
    except BaseException as error:
        report.update(status='fail', error=str(error))
        raise
    finally:
        save()


if __name__ == '__main__':
    configure_cleanup()
    parser = argparse.ArgumentParser(description=__doc__)
    for name in ('extraction', 'pdk', 'ngspice', 'work'):
        parser.add_argument('--' + name, type=Path, required=True)
    parser.add_argument('--reuse', type=Path, help='reuse completed raw cases only with byte-identical generated decks')
    run(parser.parse_args())
