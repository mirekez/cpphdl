#!/usr/bin/env python3
"""Preserve transmitter fingers, physical ports and capacitance during PEX."""
import argparse
import json
import math
from pathlib import Path
import re
import sys
import tempfile

SOURCE = Path(__file__).resolve()
sys.path.insert(0, str(SOURCE.parents[1]))
from extract_analog import digest, extract
from extracted_connections import check_capacitor_connections

CASES = dict(tx_steering=400, tx_sink=450, tx_cascode=1500,
             tx_history_cascode=500, tx_history_steering=200, tx_history_sink=150)


def check_ports(text):
    ports = re.findall(r'^port "([^"]+)" \d+ [-\d ]+ (\w+)$', text, re.M)
    if sorted(ports) != [('B', 'm2'), ('D', 'm6'), ('G', 'm2'), ('S', 'm6')]:
        raise ValueError('duplicate, missing or wrong-layer MOS bank external port')
    return dict(ports)


def run(args):
    evidence = json.loads(args.proof.read_text())
    if (evidence.get('status') != 'pass' or
            set(evidence.get('cases', {})) != set(CASES) | {'tx_missing_bank'} or
            evidence['cases']['tx_missing_bank'].get('expected_missing_bank_rejected') is not True):
        raise ValueError('complete current transmitter geometry proof required')
    hashes = dict(evidence['source_sha256'])
    if not hashes:
        raise ValueError('missing geometry source hashes')
    for case in CASES:
        record = evidence['cases'][case]
        if record.get('local_drc_violations') != 0 or not record.get('lvs'):
            raise ValueError('unqualified transmitter device geometry')
        hashes.update(record['artifact_sha256'])
    hashes.update({str(p): digest(p) for p in (SOURCE, args.proof.resolve(),
        SOURCE.parents[1] / 'extract_analog.py', SOURCE.parents[1] / 'extracted_connections.py')})
    if any(digest(path) != value for path, value in hashes.items()):
        raise ValueError('transmitter geometry evidence is stale')
    args.work.mkdir(parents=True, exist_ok=True)
    work = Path(tempfile.mkdtemp(prefix='run-', dir=args.work.resolve()))
    report = dict(status='running', work=str(work), source_sha256=hashes, cases={},
        scope='actual GDS finger/port inventory and connected conserved RC; no transistor performance or EM signoff')
    try:
        for case, width in CASES.items():
            gds = Path(evidence['work']) / case / 'device.gds'
            if evidence['cases'][case]['artifact_sha256'].get(str(gds.resolve())) != digest(gds):
                raise ValueError('extraction GDS is not the qualified layout artifact')
            result = extract(gds, 'RoutedMOS', ['D', 'G', 'S', 'B'], args.pdk,
                             args.magic, work / case, substrate='B')
            actual = result['conserved']
            fingers = width // 5
            if (actual['models'] != {'sg13_hv_nmos': fingers} or
                    any(not math.isclose(device['width_m'], 5e-6, rel_tol=1e-10) or
                        not math.isclose(device['length_m'], .45e-6, rel_tol=1e-10)
                        for device in actual['geometry'])):
                raise ValueError('extraction changed physical finger inventory/dimensions')
            if not actual['resistors'] or not any(value > 0 for _, _, value in actual['capacitors']):
                raise ValueError('missing distributed parasitics')
            ports = check_ports((work / case / 'ExtractedAnalog.ext').read_text())
            connected = check_capacitor_connections((work / case / 'conserved.spice').read_text(),
                                                    ['D', 'G', 'S', 'B'])
            report['cases'][case] = dict(fingers=fingers, total_width_um=width,
                port_layers=ports, connection_check=connected,
                extraction=str(work / case / 'extraction.json'))
        # A duplicate lower-metal injection point must fail even when it is
        # electrically connected and LVS considers the net names equivalent.
        valid = 'port "D" 1 0 0 1 1 m6\nport "G" 2 0 0 1 1 m2\n'
        valid += 'port "S" 3 0 0 1 1 m6\nport "B" 4 0 0 1 1 m2\n'
        for invalid in (valid + 'port "D" 1 0 0 1 1 m2\n',
                        valid.replace('m6', 'm2'), valid.splitlines(keepends=True)[:-1]):
            try:
                check_ports(''.join(invalid) if isinstance(invalid, list) else invalid)
            except ValueError:
                continue
            raise ValueError('invalid extracted external port contract accepted')
        if any(digest(path) != value for path, value in hashes.items()):
            raise ValueError('MOS bank qualification inputs changed during extraction')
        report['status'] = 'pass'
        print('PASS six transmitter MOS banks: all physical fingers, single external port locations, connected RC')
    except BaseException as error:
        report.update(status='fail', error=str(error))
        raise
    finally:
        payload = json.dumps(report, indent=2) + '\n'
        (work / 'checks.json').write_text(payload)
        (args.work / 'checks.json').write_text(payload)


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    for name in ('proof', 'pdk', 'magic', 'work'):
        parser.add_argument('--' + name, type=Path, required=True)
    run(parser.parse_args())
