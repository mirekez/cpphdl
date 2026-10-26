#!/usr/bin/env python3
"""Compare midpoint and original PDK resistance and low-frequency charge."""
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
from resistor_body_model import split_body
from extract_analog import digest
from input_charge import verify_files
from tool_process import configure_cleanup, execute


def run(args):
    models = args.pdk.resolve() / 'ihp-sg13g2/libs.tech/ngspice/models'
    library = (models / 'resistors_mod.lib').read_text()
    for model, contact in [('rppd', '35e-6'), ('rhigh', '80e-6')]:
        section = re.search(r'\.subckt ' + model + r' .*?\.ends ' + model, library, re.S | re.I)
        if section is None or any(token not in section[0] for token in
                ('+lhead=0.86e-6', '+rzspec=' + contact, '+a1=a a2=a', '+p1=p p2=p')):
            raise ValueError('process resistor contact/capacitance equations changed')
    files = [SOURCE, SOURCE.parents[1] / 'resistor_body_model.py', SOURCE.parents[1] / 'input_charge.py',
             SOURCE.parents[1] / 'extract_analog.py', ANALOG / 'pdk/1G_repeater_IHPSG13G2/tool_process.py',
             args.ngspice.resolve()] + list(models.rglob('*.lib')) + list(models.rglob('*.spice'))
    hashes = {str(p): digest(p) for p in files}
    args.work.mkdir(parents=True, exist_ok=True)
    work = Path(tempfile.mkdtemp(prefix='run-', dir=args.work.resolve()))
    report = dict(status='running', work=str(work), source_sha256=hashes, cases={},
        scope='R3 midpoint representation: DC and low-frequency total intrinsic capacitance only; '
              'excludes corrected extraction, field calibration, mismatch and self-heating')
    environment = dict(os.environ, PDK_ROOT=str(args.pdk.resolve()), PDK='ihp-sg13g2',
                       SPICE_USERINIT_DIR=str(models.parent), OMP_NUM_THREADS='1')

    def save():
        payload = json.dumps(report, indent=2) + '\n'
        (work / 'checks.json').write_text(payload)
        (args.work / 'checks.json').write_text(payload)

    save()
    try:
        for model, length in [('rppd', 19.19e-6), ('rppd', 38.66e-6), ('rhigh', 53.7e-6)]:
            for temperature in (-40, 27, 125):
                name = f'{model}_{length:.8g}_{temperature}'
                case = work / name
                case.mkdir()
                parameters = f'{model} w=0.5u l={length:.17g}'
                dc, geometry = split_body('dc', ['B', '0', '0'], 'body_dc', model, .5e-6, length)
                ac, _ = split_body('ac', ['COPY', 'COPY', '0'], 'body_ac', model, .5e-6, length)
                text = '\n'.join(['Contact and charge conservation of one physical resistor',
                    f'.lib "{models}/cornerRES.lib" res_typ', f'.temp {temperature}',
                    'Vtest A 0 0', 'Ecopy B 0 A 0 1', f'Xoriginal A 0 0 {parameters}', dc,
                    'Vcap CM 0 DC .6 AC 1', 'Ecap COPY 0 CM 0 1', f'Xcm CM CM 0 {parameters}', ac,
                    '.control', 'set wr_singlescale', 'set wr_vecnames', 'set numdgt=17',
                    'dc Vtest -1.32 1.32 .03', 'wrdata dc.dat v(A) i(Vtest) i(Ecopy)',
                    'ac lin 1 1 1', 'let original_cap = -imag(i(Vcap))/(6.283185307179586*frequency)',
                    'let split_cap = -imag(i(Ecap))/(6.283185307179586*frequency)',
                    'wrdata ac.dat original_cap split_cap', 'quit', '.endc', '.end', ''])
                (case / 'bench.cir').write_text(text)
                with (case / 'ngspice.log').open('w') as log:
                    execute([str(args.ngspice.resolve()), '-b', 'bench.cir'], cwd=case, env=environment,
                        stdout=log, stderr=-2, check=True, timeout=30, memory_exclusive=False)
                if re.search(r'\b(error|fatal|failed|warning)\b|unknown parameter', (case / 'ngspice.log').read_text(), re.I):
                    raise ValueError('resistor process diagnostic requires review')
                dc_rows = [[float(v) for v in line.split()] for line in (case / 'dc.dat').read_text().splitlines()[1:]]
                ac_rows = [[float(v) for v in line.split()] for line in (case / 'ac.dat').read_text().splitlines()[1:]]
                if len(dc_rows) != 89 or len(ac_rows) != 1 or any(len(row) != 4 for row in dc_rows) or len(ac_rows[0]) != 5:
                    raise ValueError('incomplete resistor characterization')
                if not all(math.isfinite(v) for row in dc_rows + ac_rows for v in row):
                    raise ValueError('nonfinite resistor observations')
                dc_error = max(abs(row[3] / row[2] - 1) for row in dc_rows if abs(row[1]) > .01)
                frequency, original, imaginary_original, split, imaginary_split = ac_rows[0]
                if frequency != 1 or imaginary_original != 0 or imaginary_split != 0:
                    raise ValueError('changed AC capacitance observation')
                cap_error = abs(split / original - 1) if original > 0 else math.inf
                if dc_error > 1e-6 or cap_error > 1e-6 or split <= 0:
                    raise ValueError(f'resistor representation changed DC or intrinsic charge: {dc_error}, {cap_error}')
                report['cases'][name] = dict(geometry=geometry, temperature_C=temperature,
                    maximum_relative_dc_error=dc_error, original_capacitance_F=original,
                    split_capacitance_F=split, relative_capacitance_error=cap_error,
                    artifact_sha256={str(case / p): digest(case / p) for p in ('bench.cir', 'dc.dat', 'ac.dat', 'ngspice.log')})
                save()
        verify_files(hashes)
        report['status'] = 'pass'
        print('PASS nine resistor-body cases: DC and intrinsic capacitance preserved')
    except BaseException as error:
        report.update(status='fail', error=str(error))
        raise
    finally:
        save()


if __name__ == '__main__':
    configure_cleanup()
    parser = argparse.ArgumentParser(description=__doc__)
    for name in ('pdk', 'ngspice', 'work'):
        parser.add_argument('--' + name, type=Path, required=True)
    run(parser.parse_args())
