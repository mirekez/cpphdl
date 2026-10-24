#!/usr/bin/env python3
"""Qualify an explicitly sized PSP follower against DC and transient contracts."""
import argparse
import gzip
import hashlib
import importlib.util
import itertools
import json
import math
import os
from pathlib import Path
import re
import shutil
import sys

SOURCE = Path(__file__).resolve()
ANALOG = SOURCE.parents[3]
sys.path.insert(0, str(SOURCE.parents[1]))
from follower_process import process_deck, dc_deck
from mos_geometry import size_mos_instances
sys.path.insert(0, str(ANALOG / '1G/tests'))
from hardware import configure_cleanup, execute, source_dependencies
spec = importlib.util.spec_from_file_location('follower_contract', ANALOG / '1G/tests/source_follower.py')
checks = importlib.util.module_from_spec(spec)
spec.loader.exec_module(checks)


def run(args):
    work = args.work.resolve()
    work.mkdir(parents=True, exist_ok=True)
    (work / 'checks.json').unlink(missing_ok=True)
    model_root = args.pdk.resolve() / 'ihp-sg13g2/libs.tech/ngspice'
    paths = [SOURCE, SOURCE.parents[1] / 'follower_process.py', SOURCE.parents[1] / 'mos_geometry.py',
             ANALOG / '1G/tests/source_follower.py', ANALOG / 'VerilogA/compare.py',
             args.bench.resolve(), args.ngspice.resolve(), model_root / '.spiceinit']
    paths += [p for p in model_root.rglob('*') if p.is_file() and p.suffix in ('.lib', '.spice', '.osdi')]
    hashes = source_dependencies([ANALOG / '1G/tests/source_follower.cpp'], ANALOG.parent, args.cxx,
        ['-std=c++20', '-I' + str(ANALOG), '-I' + str(ANALOG / 'include')])
    hashes.update({str(p): checks.digest(p) for p in paths})
    report = dict(status='running', cases={}, source_sha256=hashes,
                  scope='explicit 200 um/0.45 um HV PSP source follower with valid fingers; no layout or mismatch qualification')
    environment = dict(os.environ, PDK_ROOT=str(args.pdk.resolve()), PDK='ihp-sg13g2',
                       SPICE_USERINIT_DIR=str(model_root))

    def simulate(case, name):
        with (case / (name + '.log')).open('w') as log:
            execute([str(args.ngspice.resolve()), '-b', name + '.cir'], cwd=case, env=environment,
                    stdout=log, stderr=-2, check=True, timeout=90)
        if re.search(r'\b(error|fatal|unknown|failed|aborted)\b|not found|timestep too small',
                     (case / (name + '.log')).read_text(), re.I):
            raise ValueError('PSP follower simulation failed: ' + str(case))

    try:
        for corner, temperature, supply, load in itertools.product(
                ('mos_tt', 'mos_ss', 'mos_ff', 'mos_sf', 'mos_fs'), (-40, 27, 125), (2.97, 3.3, 3.63), (.05, 4)):
            name = f'{corner}_{temperature}_{supply}_{load}'
            case = work / name
            case.mkdir(exist_ok=True)
            with (case / 'export.log').open('w') as log:
                execute([str(args.bench.resolve()), str(case), 'export', str(supply), str(load), '10000', '5'],
                        stdout=log, stderr=-2, check=True, timeout=60)
            deck = process_deck((case / 'design.va').read_text(), (case / 'bench.cir').read_text(),
                                model_root / 'models', corner, temperature)
            deck, geometry = size_mos_instances(deck)
            (case / 'ihp.cir').write_text(deck)
            (case / 'dc.cir').write_text(dc_deck(deck))
            (case / 'mos_geometry.json').write_text(json.dumps(geometry, indent=2) + '\n')
            simulate(case, 'dc')
            with (case / 'dc.dat').open() as stream:
                next(stream)
                rows = [[float(x) for x in line.split()] for line in stream if line.strip()]
            if len(rows) != 5 or any(len(r) != 5 or not all(math.isfinite(x) for x in r) for r in rows):
                raise ValueError('incomplete/nonfinite PSP follower DC sweep')
            targets = [r[1] for r in rows]
            gains = [(b - a) / .2 for a, b in zip(targets, targets[1:])]
            dc_checks = dict(input_sweep=all(abs(r[0] - (2.1 + .2 * i)) < 1e-9 and abs(r[2] - r[0]) < 1e-4
                                            for i, r in enumerate(rows)),
                             positive_gain=all(.65 < gain < 1 for gain in gains),
                             gate_headroom=all(.5 < r[2] - r[1] < 1.6 for r in rows),
                             drain_headroom=all(r[3] - r[1] > .3 for r in rows),
                             supply_kcl=all(abs((supply - r[3]) / 10 - r[1] * (1 / 500 + 1 / 10000)
                                                - r[4] + (r[0] - r[2]) / 50) < 1e-6
                                            for r in rows))
            if not all(dc_checks.values()):
                raise ValueError('PSP follower DC contract: ' + str(dc_checks))
            simulate(case, 'ihp')
            raw = case / 'spice.dat'
            result = checks.contract(checks.read_spice(raw, checks.HEADER), supply, 10000, targets)
            checks.verify(result)
            archive = case / 'spice.dat.gz'
            raw_sha = checks.digest(raw)
            with raw.open('rb') as original, gzip.open(archive, 'wb', compresslevel=1) as output:
                shutil.copyfileobj(original, output)
            with gzip.open(archive, 'rb') as original:
                if hashlib.file_digest(original, 'sha256').hexdigest() != raw_sha:
                    raise ValueError('PSP follower archive is not lossless')
            raw.unlink()
            result.update(dc_checks=dc_checks, dc_targets_V=targets, dc_gain=gains,
                          dc_body_current_A=[r[4] for r in rows],
                          artifact_sha256={str(p): checks.digest(p) for p in
                              (archive, case / 'dc.dat', case / 'dc.cir', case / 'ihp.cir',
                               case / 'mos_geometry.json', case / 'dc.log', case / 'ihp.log')})
            report['cases'][name] = result
            (work / 'checks.json').write_text(json.dumps(report, indent=2) + '\n')
            print(name + ': pass', flush=True)
        if any(checks.digest(p) != value for p, value in hashes.items()):
            raise ValueError('PSP follower proof inputs changed during execution')
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
    run(parser.parse_args())
