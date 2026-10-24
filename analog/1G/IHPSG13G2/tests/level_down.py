#!/usr/bin/env python3
"""Run the same level-down rail/loading contract on actual IHP PSP devices."""
import argparse
import gzip
import hashlib
import itertools
import json
import os
from pathlib import Path
import re
import shutil
import sys

SOURCE = Path(__file__).resolve()
ANALOG = SOURCE.parents[3]
sys.path.insert(0, str(SOURCE.parents[1]))
from level_down_process import process_deck
from mos_geometry import size_mos_instances
sys.path.insert(0, str(ANALOG / '1G/tests'))
# The script shares its filename with the common checker; load by explicit path.
import importlib.util
spec = importlib.util.spec_from_file_location('level_down_contract', ANALOG / '1G/tests/level_down.py')
checks = importlib.util.module_from_spec(spec)
spec.loader.exec_module(checks)
from hardware import configure_cleanup, execute, source_dependencies


def run(args):
    work = args.work.resolve()
    work.mkdir(parents=True, exist_ok=True)
    model_root = args.pdk.resolve() / 'ihp-sg13g2/libs.tech/ngspice'
    paths = [SOURCE, SOURCE.parents[1] / 'level_down_process.py', SOURCE.parents[1] / 'mos_geometry.py',
             ANALOG / '1G/tests/level_down.py', ANALOG / 'VerilogA/compare.py',
             args.bench.resolve(), args.ngspice.resolve(), model_root / '.spiceinit']
    paths += [p for p in model_root.rglob('*') if p.is_file() and p.suffix in ('.lib', '.spice', '.osdi')]
    hashes = source_dependencies([ANALOG / '1G/tests/level_down.cpp'], ANALOG.parent, args.cxx,
        ['-std=c++20', '-I' + str(ANALOG), '-I' + str(ANALOG / 'include')])
    hashes.update({str(p): checks.digest(p) for p in paths})
    report = dict(status='running', cases={}, source_sha256=hashes,
                  scope='four-MOS explicitly sized IHP PSP level-down interface; no layout, mismatch or power-off qualification')
    environment = dict(os.environ, PDK_ROOT=str(args.pdk.resolve()), PDK='ihp-sg13g2',
                       SPICE_USERINIT_DIR=str(model_root))
    corners = ('mos_tt', 'mos_ss', 'mos_ff', 'mos_sf', 'mos_fs') if args.corner == 'all' else (args.corner,)
    pairs = ((1.08, 2.97), (1.2, 3.3), (1.32, 3.63), (1.08, 3.63), (1.32, 2.97))
    try:
        for corner, temperature, (core, high), load in itertools.product(corners, (-40, 27, 125), pairs, (20, 200)):
            name = f'{corner}_{temperature}_{core}_{high}_{load}'
            case = work / name
            case.mkdir(exist_ok=True)
            with (case / 'export.log').open('w') as log:
                execute([str(args.bench.resolve()), str(case), 'export', str(core), str(high), str(load), '2'],
                        stdout=log, stderr=-2, check=True, timeout=60)
            deck = process_deck((case / 'design.va').read_text(), (case / 'bench.cir').read_text(),
                                model_root / 'models', corner, temperature)
            deck, geometry = size_mos_instances(deck)
            (case / 'ihp.cir').write_text(deck)
            (case / 'mos_geometry.json').write_text(json.dumps(geometry, indent=2) + '\n')
            with (case / 'ngspice.log').open('w') as log:
                execute([str(args.ngspice.resolve()), '-b', 'ihp.cir'], cwd=case, env=environment,
                        stdout=log, stderr=-2, check=True, timeout=90)
            if re.search(r'\b(error|fatal|unknown|failed|aborted)\b|not found|timestep too small',
                         (case / 'ngspice.log').read_text(), re.I):
                raise ValueError('IHP level-down simulation failed: ' + name)
            raw = case / 'spice.dat'
            result = checks.contract(checks.read_spice(raw, checks.HEADER), core, high)
            checks.verify(result)
            archive = case / 'spice.dat.gz'
            original_sha = checks.digest(raw)
            with raw.open('rb') as original, gzip.open(archive, 'wb', compresslevel=1) as output:
                shutil.copyfileobj(original, output)
            with gzip.open(archive, 'rb') as original:
                if hashlib.file_digest(original, 'sha256').hexdigest() != original_sha:
                    raise ValueError('IHP level-down archive is not lossless')
            raw.unlink()
            result['artifact_sha256'] = {str(p): checks.digest(p) for p in
                (archive, case / 'ihp.cir', case / 'mos_geometry.json', case / 'ngspice.log')}
            report['cases'][name] = result
            (work / 'checks.json').write_text(json.dumps(report, indent=2) + '\n')
            print(name + ': pass', flush=True)
        if any(checks.digest(p) != value for p, value in hashes.items()):
            raise ValueError('process proof inputs changed during execution')
        report['status'] = 'pass'
    except BaseException as error:
        report.update(status='fail', error=str(error))
        raise
    finally:
        (work / 'checks.json').write_text(json.dumps(report, indent=2) + '\n')


if __name__ == '__main__':
    configure_cleanup()
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--work', type=Path, required=True)
    parser.add_argument('--bench', type=Path, required=True)
    parser.add_argument('--pdk', type=Path, required=True)
    parser.add_argument('--ngspice', type=Path, required=True)
    parser.add_argument('--cxx', default='/usr/bin/c++')
    parser.add_argument('--corner', choices=('all', 'mos_tt', 'mos_ss', 'mos_ff', 'mos_sf', 'mos_fs'), default='all')
    run(parser.parse_args())
