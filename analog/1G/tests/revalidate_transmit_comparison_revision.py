#!/usr/bin/env python3
"""Revalidate complete waveforms after the exact JSON comparison-only fix.

All sources, models and tools must match their recorded hashes except for the
one precisely verified comparison expression. Keep the old receipt and source.
This checks retained data, and does not claim a new transient simulation.
"""
import argparse
import json
from pathlib import Path

from comparison_revision import verify_json_roundtrip_revision
from core_transmit_driver import ANALOG, HEADER, TRIM_HEADER
from core_transmit_states import fresh
from transmit_driver import controls, digest
from transmit_state_contract import check
from trace_states import read_states


def revalidate(args):
    args.output.parent.mkdir(parents=True, exist_ok=True)
    args.output.unlink(missing_ok=True)
    original = json.loads(args.report.read_text())
    if original.get('status') != 'pass':
        raise ValueError('original individual electrical checks did not pass')
    checker = Path(__file__).with_name('core_transmit_states.py').resolve()
    expected = original['source_sha256'].get(str(checker))
    if not expected or digest(args.archived_checker) != expected:
        raise ValueError('archived checker differs from the recorded simulation harness')
    verify_json_roundtrip_revision(args.archived_checker.read_text(), checker.read_text())
    hashes = dict(original['source_sha256'])
    hashes[str(checker)] = digest(checker)
    tools = original.get('binary_sha256', {})
    fresh(hashes | tools)
    extras = [Path(__file__).resolve(), Path(__file__).with_name('comparison_revision.py').resolve(),
              args.archived_checker.resolve()]
    hashes |= {str(path): digest(path) for path in extras}
    retained = {str(args.report.resolve()): digest(args.report)}
    cases = original.get('cases') or {'process': original}
    for name, result in cases.items():
        fresh(result['artifact_sha256'])
        paths = [Path(path) for path in result['artifact_sha256']]
        waves = [path for path in paths if path.name in ('trace.csv.gz', 'spice.dat.gz')]
        if len(waves) != 1:
            raise ValueError('ambiguous retained waveform')
        wave = waves[0]
        case = wave.parent
        interface = json.loads((case / 'interface.json').read_text())
        header = HEADER + TRIM_HEADER if interface.get('trim_code') is not None else HEADER
        audit = case / 'spice.dat.roundoff.json'
        rows = read_states(wave, header, csv_format=wave.name == 'trace.csv.gz', audit=audit if audit.exists() else None)
        current = json.loads(json.dumps(check(rows, controls(case / 'controls.csv'), interface)))
        if any(result.get(key) != value for key, value in current.items()):
            raise ValueError('retained physical measurements changed: ' + name)
        del rows
        retained |= result['artifact_sha256']
        print(name, current['maximum_all_state_plateau_error_V'], flush=True)
    fresh(hashes | tools | retained)
    report = dict(original)
    report['source_sha256'] = hashes
    report['revalidation'] = dict(original_receipt=str(args.report.resolve()),
        original_receipt_sha256=retained[str(args.report.resolve())], archived_checker=str(args.archived_checker.resolve()),
        permitted_revision='only saved JSON array versus in-memory tuple comparison; entire remaining Python AST identical',
        scope='fresh physical checks of retained complete data; unchanged circuit, tools, commands and tolerances; no new simulation')
    args.output.write_text(json.dumps(report, indent=2) + '\n')


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--report', type=Path, required=True)
    parser.add_argument('--archived-checker', type=Path, required=True)
    parser.add_argument('--output', type=Path, required=True)
    revalidate(parser.parse_args())
