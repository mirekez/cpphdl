#!/usr/bin/env python3
"""Audit a preserved full SRAM extraction; this does not qualify chip timing or PEX."""
import argparse
import hashlib
import json
from pathlib import Path
import time

import klayout
import klayout.db as db
import klayout.dbcore as dbcore

from sram_lvs_audit import sheet_resistance
from sram_lvs_compare import Correspondence


def digest(path):
    with Path(path).open('rb') as stream:
        return hashlib.file_digest(stream, 'sha256').hexdigest()


def run(args):
    work = args.output.resolve()
    work.mkdir(parents=True, exist_ok=False)
    result = dict(status='running', full_sram_qualified=False, full_chip_qualified=False)
    started = time.monotonic()
    try:
        previous = json.loads(args.extraction_receipt.read_text())
        upstream = previous.get('source_sha256')
        if not upstream or not isinstance(upstream, dict):
            raise ValueError('extraction receipt lacks input hashes')
        if not args.source.resolve().is_relative_to(args.extraction_receipt.resolve().parent):
            raise ValueError('database must belong to the supplied extraction run')
        artifacts = previous.get('artifact_sha256', {})
        if artifacts.get(str(args.source.resolve())) != digest(args.source):
            raise ValueError('database differs from the completed extraction artifact')
        for name, expected in upstream.items():
            if digest(name) != expected:
                raise ValueError('extraction input changed: ' + name)
        here = Path(__file__).resolve().parent
        inputs = [args.source, args.reference, args.extraction_receipt, args.resistor_deck,
                  Path(__file__).resolve(), here / 'sram_lvs_compare.py', here / 'sram_lvs_audit.py',
                  Path(dbcore.__file__)]
        hashes = {str(p.resolve()): digest(p) for p in inputs}
        result.update(source_sha256=hashes, klayout_version=klayout.__version__)
        lines = args.reference.read_text().replace('\n+ ', ' ').splitlines()
        headers = [line.split() for line in lines
                   if line.upper().startswith('.SUBCKT ' + args.top.upper() + ' ')]
        if len(headers) != 1:
            raise ValueError('missing or ambiguous reference interface')
        ports = set(headers[0][2:])
        if len(ports) != len(headers[0][2:]):
            raise ValueError('duplicate reference port')
        database = db.LayoutVsSchematic()
        database.read(str(args.source))
        comparison = Correspondence(database, args.top)
        if args.expand_column_logic:
            result['boundary_expansion'] = comparison.expand_column_logic()
        result.update(comparison.run(ports, sheet_resistance(args.resistor_deck)))
        output = work / 'macro.lvsdb'
        database.write(str(output))
        for name, expected in {**upstream, **artifacts, **hashes}.items():
            if digest(name) != expected:
                raise ValueError('input changed during comparison: ' + name)
        result.update(seconds=time.monotonic() - started, database=str(output),
                      database_sha256=digest(output))
        print('PASS extracted SRAM graph:', output, flush=True)
    except BaseException as error:
        result.update(status='fail', error=str(error))
        raise
    finally:
        (work / 'checks.json').write_text(json.dumps(result, indent=2) + '\n')


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    for name in ('source', 'reference', 'extraction-receipt', 'resistor-deck', 'output'):
        parser.add_argument('--' + name, type=Path, required=True)
    parser.add_argument('--top', default='RM_IHPSG13_2P_1024X16_C2_BM_BIST')
    parser.add_argument('--expand-column-logic', action='store_true')
    run(parser.parse_args())
