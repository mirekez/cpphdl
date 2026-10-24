#!/usr/bin/env python3
"""Keep the unsupported high-common-mode linearity failure as a strict witness."""
import argparse
import json
from pathlib import Path
from types import SimpleNamespace

from differential_receive_gain import run, contract, digest, HEADER, fresh
from trace_states import read_states
from hardware import configure_cleanup


def check(args):
    args.work.mkdir(parents=True, exist_ok=True)
    receipt = args.receipt
    if receipt is None:
        options = SimpleNamespace(flow='psp', work=args.work / 'characterization', bench=args.bench,
            ngspice=args.ngspice, pdk=args.pdk, cxx=args.cxx, corner='mos_ss', temperature=125,
            codes=[4], common=[1.65], step_ps=5)
        try:
            run(options)
        except ValueError:
            pass
        else:
            raise ValueError('high-common-mode characterization unexpectedly passes; review the operating boundary')
        receipt = options.work / 'psp/checks.json'
    previous = json.loads(receipt.read_text())
    if (previous.get('status') != 'fail' or previous.get('flow') != 'psp' or
            previous.get('corner') != 'mos_ss' or previous.get('temperature_C') != 125):
        raise ValueError('the retained slow/hot failing process characterization is required')
    fresh(previous['source_sha256'])
    case = previous['cases']['4_1.65']
    wave = Path(case['waveform'])
    if digest(wave) != case['waveform_sha256']:
        raise ValueError('changed high-common-mode waveform')
    result = contract(read_states(wave, HEADER), 4)
    if any(case.get(key) != value for key, value in result.items()):
        raise ValueError('changed independently checked high-common-mode results')
    if {key for key, value in result['checks'].items() if not value} != {'linearity'}:
        raise ValueError('the stress witness must fail only its unchanged 15 mV linearity bound')
    if not all(1.60 <= common <= 1.70 for common in result['common_mode_range_V']):
        raise ValueError('stress witness is not at the intended high common mode')
    report = dict(status='rejection_verified', checks=result, process_receipt=str(receipt.resolve()),
        receipt_sha256=digest(receipt), checker_sha256=digest(Path(__file__)),
        scope='15 mV linearity rejection at 1.65 V source common mode, SS/125 C; not a passing electrical operating point; receiver operation is checked separately at 1.1/1.4 V source common mode and in connected input-range sweeps')
    (args.work / 'checks.json').write_text(json.dumps(report, indent=2) + '\n')
    print('PASS retained stress rejection; linearity residual V', result['maximum_linearity_residual_V'])


if __name__ == '__main__':
    configure_cleanup()
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--receipt', type=Path)
    parser.add_argument('--work', type=Path, required=True)
    parser.add_argument('--bench', type=Path)
    parser.add_argument('--ngspice', type=Path)
    parser.add_argument('--pdk', type=Path)
    parser.add_argument('--cxx', default='/usr/bin/c++')
    args = parser.parse_args()
    if args.receipt is None and not all((args.bench, args.ngspice, args.pdk)):
        parser.error('a fresh characterization needs --bench, --ngspice and --pdk')
    check(args)
