#!/usr/bin/env python3
"""Qualify the optional bias reference-access layout with unchanged local gates."""
import argparse
import importlib.util
import json
from pathlib import Path
import sys

SOURCE = Path(__file__).resolve()
sys.path.insert(0, str(SOURCE.parents[1]))
from extract_analog import digest
from input_charge import verify_files
from tool_process import configure_cleanup
from transmit_bias_layout import create_transmit_bias
from transmit_bias_reference_access import add_reference_access


def candidate(pdk):
    layout, top, geometry = create_transmit_bias(pdk)
    add_reference_access(layout, top, geometry)
    return layout, top, geometry


def run(args):
    spec = importlib.util.spec_from_file_location('bias_local_test', SOURCE.with_name('transmit_bias.py'))
    test = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(test)
    hashes = {str(p): digest(p) for p in
              (SOURCE, SOURCE.parents[1] / 'transmit_bias_reference_access.py')}
    test.create_transmit_bias = candidate
    try:
        test.run(args)
    finally:
        receipt = args.work / 'checks.json'
        if receipt.exists():
            report = json.loads(receipt.read_text())
            report['source_sha256'].update(hashes)
            report['layout_variant'] = 'parallel reference finger access'
            report['complete_transmitter_qualified'] = False
            try:
                verify_files(hashes)
            except BaseException as error:
                report.update(status='fail', error=str(error))
                raise
            finally:
                payload = json.dumps(report, indent=2) + '\n'
                receipt.write_text(payload)
                (Path(report['work']) / 'checks.json').write_text(payload)


if __name__ == '__main__':
    configure_cleanup()
    parser = argparse.ArgumentParser(description=__doc__)
    for name in ('pdk', 'tools', 'magic', 'ngspice', 'work'):
        parser.add_argument('--' + name, type=Path, required=True)
    run(parser.parse_args())
