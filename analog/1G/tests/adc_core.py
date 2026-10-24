#!/usr/bin/env python3
"""Close SAR feedback through physical level-down gates and normalize its data."""
import argparse
import json
from pathlib import Path

from adc_array import check, digest, live_flow
from hardware import configure_cleanup, execute, source_dependencies

SOURCE = Path(__file__).resolve()
ANALOG = SOURCE.parents[2]


def run(args):
    work = args.work.resolve()
    work.mkdir(parents=True, exist_ok=True)
    (work.parent / 'comparison.json').unlink(missing_ok=True)
    hashes = source_dependencies([SOURCE.with_suffix('.cpp')], ANALOG.parent, args.cxx,
        ['-std=c++20', '-I' + str(ANALOG), '-I' + str(ANALOG / 'include'), '-I/usr/include/eigen3'])
    for path in (SOURCE, SOURCE.with_name('adc_array.py'), ANALOG / 'VerilogA/live.py', ANALOG / 'VerilogA/compare.py'):
        hashes[str(path)] = digest(path)
    binaries = {str(args.bench.resolve()): digest(args.bench)}
    if args.flow == 'veriloga':
        binaries.update({str(p.resolve()): digest(p) for p in (args.openvaf, args.ngspice)})
    report = dict(status='running', flow=args.flow, slices=args.slices, bursts=args.bursts,
                  source_sha256=hashes, binary_sha256=binaries,
                  scope='electrical SAR/latch/core-interface feedback into composed C++RTL normalization; '
                        'host clock and high-voltage controls, generic MOS, no IHP ADC or full receiver qualification')
    try:
        if args.flow == 'cpp':
            with (work / 'native.log').open('w') as log:
                execute([str(args.bench.resolve()), str(work), 'run', str(args.slices), '.25', str(args.bursts)],
                        stdout=log, stderr=-2, check=True, timeout=10800)
        else:
            live_flow(args, work)
        report.update(check(work, args.flow, args.slices, args.bursts, core=True))
        if any(digest(Path(p)) != value for p, value in (hashes | binaries).items()):
            raise ValueError('ADC core proof inputs changed during execution')
        report['status'] = 'pass'
        print('PASS', args.flow, args.slices, 'slices; core decisions and normalized codes:', report['normalized_codes'])
    except BaseException as error:
        report.update(status='fail', error=str(error))
        raise
    finally:
        (work / 'checks.json').write_text(json.dumps(report, indent=2) + '\n')


def compare(args):
    work = args.work.resolve()
    (work / 'comparison.json').unlink(missing_ok=True)
    reports = []
    for flow in ('cpp', 'veriloga'):
        report = json.loads((work / flow / 'checks.json').read_text())
        if (report.get('status') != 'pass' or report.get('flow') != flow or
                report.get('slices') != args.slices or report.get('bursts') != args.bursts):
            raise ValueError('both matching core ADC flows must pass independently')
        if any(digest(Path(p)) != value for p, value in (report['source_sha256'] | report['binary_sha256']).items()):
            raise ValueError('stale core ADC proof')
        current = check(work / flow, flow, args.slices, args.bursts, core=True)
        if any(report[k] != value for k, value in current.items()):
            raise ValueError('core ADC absolute evidence changed')
        reports.append(report)
    if reports[0]['source_sha256'] != reports[1]['source_sha256']:
        raise ValueError('core ADC flows tested different circuits or controllers')
    differences = {}
    for key, tolerance in (('codes', 1), ('normalized_codes', 2)):
        a, b = [r[key] for r in reports]
        if len(a) != args.slices * args.bursts or len(b) != len(a):
            raise ValueError('core ADC code coverage differs')
        delta = [abs(x - y) for x, y in zip(a, b)]
        if max(delta) > tolerance:
            raise ValueError('core ADC solver disagreement: ' + key)
        differences[key] = delta
    (work / 'comparison.json').write_text(json.dumps(dict(status='pass', code_differences=differences,
        flow_report_sha256={flow: digest(work / flow / 'checks.json') for flow in ('cpp', 'veriloga')}), indent=2) + '\n')


if __name__ == '__main__':
    configure_cleanup()
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--flow', choices=('cpp', 'veriloga', 'compare'), required=True)
    parser.add_argument('--work', type=Path, required=True)
    parser.add_argument('--bench', type=Path)
    parser.add_argument('--slices', type=int, choices=(1, 4, 16), required=True)
    parser.add_argument('--bursts', type=int, choices=range(1, 17), default=1)
    parser.add_argument('--openvaf', type=Path)
    parser.add_argument('--ngspice', type=Path)
    parser.add_argument('--cxx', default='/usr/bin/c++')
    args = parser.parse_args()
    if args.bursts != 1 and args.slices != 16:
        parser.error('repeated operation requires sixteen physical slices')
    compare(args) if args.flow == 'compare' else run(args)
