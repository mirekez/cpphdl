#!/usr/bin/env python3
"""Compare actual extracted clock-pin capacitance before and after placement."""
import argparse
from collections import Counter
import json
from pathlib import Path

from package_development_layout import digest


def read_checked(path):
    report = json.loads(path.read_text())
    if report.get('status') != 'pass':
        raise ValueError('requires completed, conserved RC extraction')
    for field in ('source_sha256', 'artifact_sha256'):
        for name, expected in report[field].items():
            if digest(Path(name)) != expected:
                raise ValueError('changed extraction input: ' + name)
    return report


def compare(before, after):
    if before['ports'] != after['ports'] or before['top'] != after['top']:
        raise ValueError('clock interfaces differ')
    def devices(report):
        return Counter((item['model'], item['width_m'], item['length_m'])
                       for item in report['capacitance']['geometry'])
    if devices(before) != devices(after):
        raise ValueError('clock device geometry or quantities differ')
    pins = {}
    for net in ('RAW', 'ADMIT', 'OUT'):
        values = [sum(value for a, b, value in report['capacitance']['capacitors'] if net in (a, b))
                  for report in (before, after)]
        if min(values) <= 0:
            raise ValueError('clock extraction lacks pin capacitance: ' + net)
        pins[net] = dict(before_fF=values[0] * 1e15, after_fF=values[1] * 1e15,
                         change_percent=100 * (values[1] / values[0] - 1))
    return dict(status='pin_load_screen_pass' if all(p['change_percent'] <= 0 for p in pins.values())
                else 'pin_load_regression', pins=pins, electrical_qualified=False,
                scope='summed extracted parasitic capacitance; excludes device capacitance, waveform and full timing qualification')


def run(args):
    before, after = read_checked(args.before), read_checked(args.after)
    result = compare(before, after)
    gdss = [path for path in after['source_sha256'] if Path(path).suffix == '.gds']
    if len(gdss) != 1:
        raise ValueError('ambiguous extracted candidate GDS')
    result.update(compact_gds_sha256=after['source_sha256'][gdss[0]],
                  before_receipt=str(args.before.resolve()), after_receipt=str(args.after.resolve()),
                  source_sha256={str(p.resolve()): digest(p) for p in (args.before, args.after, Path(__file__))})
    args.output.write_text(json.dumps(result, indent=2) + '\n')
    print(json.dumps(result['pins']))
    return 0 if result['status'] == 'pin_load_screen_pass' else 1


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    for name in ('before', 'after', 'output'):
        parser.add_argument('--' + name, type=Path, required=True)
    raise SystemExit(run(parser.parse_args()))
