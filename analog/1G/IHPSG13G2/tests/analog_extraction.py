#!/usr/bin/env python3
"""Exercise real MIM extraction and reject device-capacitance double counting."""
import argparse
import json
from pathlib import Path
import sys
import tempfile

import klayout.db as db

SOURCE = Path(__file__).resolve()
sys.path.insert(0, str(SOURCE.parents[1]))
from capacitor_layout import append_mim
from capacitor_series_layout import append_mim_series
from extract_analog import extract, digest


def run(args):
    args.work.mkdir(parents=True, exist_ok=True)
    work = Path(tempfile.mkdtemp(prefix='run-', dir=args.work.resolve()))
    tracked = [SOURCE, SOURCE.parents[1] / 'capacitor_layout.py',
               SOURCE.parents[1] / 'capacitor_series_layout.py', SOURCE.parents[1] / 'mos_layout.py']
    tracked += [p for p in (args.pdk / 'ihp-sg13g2/libs.tech/klayout/python').rglob('*')
                if p.is_file() and p.suffix in ('.json', '.py')]
    hashes = {str(p.resolve()): digest(p) for p in tracked}
    report = dict(status='running', work=str(work), source_sha256=hashes, cases={},
                  scope='isolated MIM device and stray-capacitance extraction guards; excludes field-solver accuracy')
    try:
        for name, value, make, count in [('Single', 4e-15, append_mim, 1), ('Series', 2e-15, append_mim_series, 2)]:
            destination = work / name
            destination.mkdir()
            layout = db.Layout()
            layout.dbu = .001
            _, geometry = make(layout, args.pdk.resolve(), name, value)
            gds = destination / 'storage.gds'
            layout.write(str(gds))
            result = extract(gds, name, ['PLUS', 'MINUS'], args.pdk, args.magic,
                             destination / 'extraction', substrate='sub')
            net = result['capacitance']
            if net['models'] != {'cap_cmim': count}:
                raise ValueError('MIM device extraction lost or duplicated a plate')
            unit = geometry if count == 1 else geometry['unit']
            for device in net['geometry']:
                if (abs(device['width_m'] * 1e6 - unit['width_um']) > 1e-8
                        or abs(device['length_m'] * 1e6 - unit['length_um']) > 1e-8):
                    raise ValueError('MIM extracted dimensions changed')
            # The PDK device model supplies ~4 fF per physical plate. Counting
            # that again as a stray to substrate must fail. Nonzero true plate
            # fringe and series-junction load must remain in the circuit.
            stray = sum(v for a, b, v in net['capacitors'])
            if not .05e-15 < stray < count * .30 * unit['pcell_nominal_capacitance_F']:
                raise ValueError('missing strays or double-counted MIM storage')
            internal = {n for row in net['capacitors'] for n in row[:2]} - {'PLUS', 'MINUS', 'sub'}
            if len(internal) != count - 1:
                raise ValueError('series MIM junction was lost or merged into a port')
            if count == 2:
                middle = next(iter(internal))
                junction_load = sum(v for a, b, v in net['capacitors'] if {a, b} == {middle, 'sub'})
                if not .1e-15 < junction_load < unit['pcell_nominal_capacitance_F'] / 2:
                    raise ValueError('missing or implausible series-junction substrate load')
            report['cases'][name] = dict(stray_F=stray, devices=count, geometry=geometry,
                extraction=str(destination / 'extraction/extraction.json'), gds_sha256=digest(gds))
        if any(digest(p) != expected for p, expected in hashes.items()):
            raise ValueError('MIM extraction source changed during the run')
        report['status'] = 'pass'
        print('PASS single and series MIM extraction: real device dimensions, strays and floating junction preserved')
    except BaseException as error:
        report.update(status='fail', error=str(error))
        raise
    finally:
        payload = json.dumps(report, indent=2) + '\n'
        (work / 'checks.json').write_text(payload)
        (args.work / 'checks.json').write_text(payload)


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    for name in ('pdk', 'magic', 'work'):
        parser.add_argument('--' + name, type=Path, required=True)
    run(parser.parse_args())
