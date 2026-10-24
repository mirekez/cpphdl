#!/usr/bin/env python3
"""Run the elaborated weighted-driver topology with actual IHP PSP devices."""
import argparse
from collections import Counter
import csv
import gzip
import hashlib
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
from mos_geometry import size_mos_instances
sys.path.insert(0, str(ANALOG / '1G/tests'))
from transmit_driver import HEADER, contract, controls, digest, verify
from hardware import configure_cleanup, execute, source_dependencies
sys.path.insert(0, str(ANALOG / 'VerilogA'))
from compare import read_spice


def process_deck(work, entry, corner):
    with (work / 'process_devices.csv').open() as file:
        devices = list(csv.DictReader(file))
    sizes = Counter((float(e['width_um']), float(e['length_um'])) for e in devices if e['kind'] == 'N')
    if sizes != Counter({(100., .45): 17, (450., .45): 4, (150., .45): 4}):
        raise ValueError('elaborated driver no longer has the explicitly sized 25-MOS topology')
    original = (work / 'bench.cir').read_text().splitlines()
    deck = [original[0], f'.lib "{entry}" {corner}']
    for line in original[1:]:
        if line.startswith(('.model CircuitModel ', 'Ndesign ', 'Cdevice', 'pre_osdi ')):
            continue
        if line == '.control':
            for e in devices:
                kind, name = e['kind'], 'physical' + e['id']
                a, b, gate = (e[key] for key in ('a', 'b', 'gate'))
                if not all(re.fullmatch(r'0|n[0-9]+', node) for node in (a, b, gate)):
                    raise ValueError('invalid process-export terminal')
                if kind in ('R', 'C'):
                    value = float(e['value'])
                    if not math.isfinite(value) or value <= 0:
                        raise ValueError('nonpositive physical passive value')
                    deck.append(f'{kind}{name} {a} {b} {value:.17g}' + (' IC=0' if kind == 'C' else ''))
                elif kind == 'N':
                    deck.append(f'X{name} {a} {gate} {b} 0 sg13_hv_nmos '
                                f'w={float(e["width_um"]):.17g}u l={float(e["length_um"]):.17g}u')
                else:
                    raise ValueError('unbound process element kind')
        deck.append(line)
    if any('CircuitModel' in line or 'design.osdi' in line for line in deck):
        raise ValueError('generic Verilog-A device remained in process deck')
    return '\n'.join(deck) + '\n'


def run(args):
    work = args.work.resolve()
    work.mkdir(parents=True, exist_ok=True)
    report_path = work / 'checks.json'
    report_path.unlink(missing_ok=True)
    technology = args.pdk.resolve() / 'ihp-sg13g2'
    entry = technology / 'libs.tech/ngspice/models/cornerMOShv.lib'
    paths = [SOURCE, SOURCE.parents[1] / 'mos_geometry.py', ANALOG / '1G/tests/transmit_driver.py', ANALOG / 'VerilogA/compare.py', args.bench.resolve(), args.ngspice.resolve()]
    paths += [p for p in (technology / 'libs.tech/ngspice/models').rglob('*') if p.is_file()]
    paths += [p for p in (technology / 'libs.tech/ngspice/osdi').glob('*.osdi') if p.is_file()]
    paths += [technology / 'libs.tech/ngspice/.spiceinit']
    hashes = source_dependencies([ANALOG / '1G/tests/transmit_driver.cpp'], ANALOG.parent, args.cxx,
        ['-std=c++20', '-I' + str(ANALOG), '-I' + str(ANALOG / 'include')])
    hashes.update({str(path): digest(path) for path in paths})
    report = dict(status='running', corner=args.corner, source_sha256=hashes,
                  scope='one nominal loaded 125 MBd IHP PSP driver; explicit geometry, inherited passive topology and gate charge; no full PVT or layout qualification')
    try:
        with (work / 'export.log').open('w') as log:
            execute([str(args.bench.resolve()), str(work), 'export', '100', '0', '1'],
                    stdout=log, stderr=-2, check=True, timeout=60)
        deck, geometry = size_mos_instances(process_deck(work, entry, args.corner))
        (work / 'mos_geometry.json').write_text(json.dumps(geometry, indent=2) + '\n')
        (work / 'ihp.cir').write_text(deck)
        environment = dict(os.environ, PDK_ROOT=str(args.pdk.resolve()), PDK='ihp-sg13g2',
                           SPICE_USERINIT_DIR=str(technology / 'libs.tech/ngspice'))
        with (work / 'ngspice.log').open('w') as log:
            execute([str(args.ngspice.resolve()), '-b', 'ihp.cir'], cwd=work, env=environment,
                    stdout=log, stderr=-2, check=True, timeout=600)
        if re.search(r'\b(error|fatal|unknown|failed|aborted)\b|not found|timestep too small',
                     (work / 'ngspice.log').read_text(), re.I):
            raise ValueError('IHP weighted-driver transistor simulation failed')
        result = contract(read_spice(work / 'spice.dat', HEADER), controls(work / 'controls.csv'),
                          json.loads((work / 'interface.json').read_text()))
        report.update(result)
        trace = work / 'spice.dat'
        compressed = work / 'spice.dat.gz'
        with trace.open('rb') as source, gzip.open(compressed, 'wb', compresslevel=6) as destination:
            shutil.copyfileobj(source, destination)
        restored = hashlib.sha256()
        with gzip.open(compressed, 'rb') as source:
            for block in iter(lambda: source.read(1024 * 1024), b''):
                restored.update(block)
        if restored.hexdigest() != digest(trace):
            raise ValueError('IHP driver archive is not lossless')
        trace.unlink()
        report['artifact_sha256'] = {str(path): digest(path) for path in
            (compressed, work / 'ihp.cir', work / 'process_devices.csv', work / 'controls.csv', work / 'ngspice.log')}
        verify(result)
        if any(digest(Path(path)) != expected for path, expected in hashes.items()):
            raise ValueError('IHP driver sources/models changed during qualification')
        report['status'] = 'pass'
        print('PASS nominal IHP PSP weighted driver; all seventeen levels, finite supply and shared bias')
    except BaseException as error:
        report.update(status='failed', error=str(error))
        raise
    finally:
        report_path.write_text(json.dumps(report, indent=2) + '\n')


if __name__ == '__main__':
    configure_cleanup()
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--work', type=Path, required=True)
    parser.add_argument('--bench', type=Path, required=True)
    parser.add_argument('--pdk', type=Path, required=True)
    parser.add_argument('--ngspice', type=Path, required=True)
    parser.add_argument('--cxx', default='c++')
    parser.add_argument('--corner', choices=('mos_tt', 'mos_ss', 'mos_ff'), default='mos_tt')
    run(parser.parse_args())
