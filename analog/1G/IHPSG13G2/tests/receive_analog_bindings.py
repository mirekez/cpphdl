#!/usr/bin/env python3
"""Reject changed storage, geometry, supply bodies and missing exported laws."""
import argparse
from collections import Counter
import csv
import io
from pathlib import Path
import re
import subprocess
import sys
import tempfile

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))
from receive_gain_process import process_deck


def rewritten(inventory, mutate):
    rows = list(csv.DictReader(io.StringIO(inventory)))
    mutate(rows)
    output = io.StringIO()
    writer = csv.DictWriter(output, fieldnames=rows[0].keys())
    writer.writeheader()
    writer.writerows(rows)
    return output.getvalue()


def check(args):
    cases = [('single_ended', args.single, ['4', '20']),
             ('differential', args.differential, ['8', '1.4', '5']),
             ('reference', args.reference, [])]
    rejected = 0
    with tempfile.TemporaryDirectory(prefix='receive-analog-bindings-') as temporary:
        for profile, bench, parameters in cases:
            directory = Path(temporary) / profile
            subprocess.run([str(bench.resolve()), str(directory), 'export', *parameters], check=True)
            model, deck, inventory = [(directory / name).read_text() for name in
                                       ('design.va', 'bench.cir', 'process_devices.csv')]

            def bind(m=model, d=deck, i=inventory, p=profile):
                return process_deck(m, d, i, Path('/reviewed-models'), profile=p)

            physical, metadata = bind()
            counts = Counter(row['kind'] for row in csv.DictReader(io.StringIO(inventory)))
            assert metadata['mos_count'] == counts['N'] + counts['P']
            assert metadata['resistor_count'] == counts['R']
            assert metadata['capacitor_count'] == counts['C']
            assert 'cpphdl_channel' not in physical and 'design.osdi' not in physical

            def change_body(rows):
                next(row for row in rows if row['kind'] == 'P')['body'] = '0'

            def change_width(rows):
                next(row for row in rows if row['kind'] == 'N')['width_um'] = '101'

            def change_storage(rows):
                next(row for row in rows if row['kind'] == 'C')['initial'] = '.025'

            deleted_law = re.sub(r'^.*I\(n\d+, n\d+\) <\+.*\n', '', model, count=1, flags=re.M)
            if deleted_law == model:
                raise ValueError('missing test contribution to delete')
            invalid = [dict(i=rewritten(inventory, change_body)),
                       dict(i=rewritten(inventory, change_width)),
                       dict(i=rewritten(inventory, change_storage)),
                       dict(m=deleted_law), dict(p='unknown')]
            for parameters in invalid:
                try:
                    bind(**parameters)
                except ValueError:
                    rejected += 1
                else:
                    raise ValueError('invalid receive analog binding was accepted')
            print('PASS', profile, 'actual IR inventory and five negative controls')
    if rejected != 15:
        raise ValueError('incomplete binding negative controls')


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    for name in ('single', 'differential', 'reference'):
        parser.add_argument('--' + name, type=Path, required=True)
    check(parser.parse_args())
