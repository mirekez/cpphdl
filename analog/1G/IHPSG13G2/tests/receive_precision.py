#!/usr/bin/env python3
"""Apply the shared acquisition limits with an explicit receiver load profile."""
import argparse
import csv
import gzip
import hashlib
import json
from pathlib import Path
import shutil
import sys

SOURCE = Path(__file__).resolve()
ANALOG = SOURCE.parents[3]
sys.path[:0] = [str(ANALOG / '1G/tests'), str(ANALOG / 'VerilogA')]
from receive_frontend import CASES as STANDARD_CASES, contract, verify, digest
from compare import read_cpp, read_spice, checked_rows
from hardware import configure_cleanup, execute, source_dependencies


def cases(profile):
    result = dict(STANDARD_CASES)
    if profile == 'parallel':
        # Retain the former overload as an additional positive load check.
        # Drive strength changed; the negative stimulus must exceed its range.
        result['large_load'] = result.pop('overload')
        result['overload'] = (4, 400, 10)
    elif profile != 'standard':
        raise ValueError('unknown receive load profile')
    return result


def run(args):
    case_definitions = cases(args.load_profile)
    work = args.work.resolve()
    work.mkdir(parents=True, exist_ok=True)
    (work / 'checks.json').unlink(missing_ok=True)
    (work.parent / 'comparison.json').unlink(missing_ok=True)
    hashes = source_dependencies([args.fixture_source.resolve()], ANALOG.parent, args.cxx,
        ['-std=c++20', '-I' + str(ANALOG), '-I' + str(ANALOG / 'include'), '-I/usr/include/eigen3'])
    hashes.update({str(path): digest(path) for path in (SOURCE, ANALOG / '1G/tests/receive_frontend.py',
                                                       ANALOG / 'VerilogA/compare.py')})
    binaries = {str(args.bench.resolve()): digest(args.bench)}
    if args.flow == 'veriloga':
        binaries.update({str(path.resolve()): digest(path) for path in (args.openvaf, args.ngspice)})
    report = dict(status='running', flow=args.flow, cases={}, source_sha256=hashes, binary_sha256=binaries,
                  scope='derived receive IC using generic MOS equations; independent PSP checks are separate',
                  fixture_source=str(args.fixture_source.resolve()), load_profile=args.load_profile,
                  case_definitions={name: list(values) for name, values in case_definitions.items()})
    try:
        for name, (count, load, step) in case_definitions.items():
            case = work / name
            case.mkdir(exist_ok=True)
            with (case / 'bench.log').open('w') as log:
                execute([str(args.bench.resolve()), str(case), 'run' if args.flow == 'cpp' else 'export',
                         str(count), str(load), str(step)], stdout=log, stderr=-2, check=True, timeout=1800)
            header = ['time_s', 'PLUS', 'MINUS', 'DRAIN', 'BUFFER', 'VDD', 'VLOGIC']
            header += ['HELD' + str(i) for i in range(count)]
            if args.flow == 'cpp':
                raw = case / 'cpp/trace.csv'
                actual, rows = read_cpp(raw)
                if actual != header:
                    raise ValueError('changed precision receive observations')
            else:
                for label, command in (('compile', [args.openvaf.resolve(), 'design.va', '-o', 'design.osdi']),
                                       ('ngspice', [args.ngspice.resolve(), '-n', '-b', 'bench.cir'])):
                    with (case / (label + '.log')).open('w') as log:
                        execute(list(map(str, command)), cwd=case, stdout=log, stderr=-2, check=True, timeout=1800)
                raw = case / 'spice.dat'
                rows = read_spice(raw, header)
            result = contract(rows, count, step)
            verify(result, name == 'overload')
            archive = raw.with_suffix(raw.suffix + '.gz')
            sha = digest(raw)
            with raw.open('rb') as source, gzip.open(archive, 'wb', compresslevel=1) as output:
                shutil.copyfileobj(source, output)
            with gzip.open(archive, 'rb') as source:
                if hashlib.file_digest(source, 'sha256').hexdigest() != sha:
                    raise ValueError('precision receive archive is not lossless')
            raw.unlink()
            result.update(waveform=str(archive), waveform_sha256=digest(archive))
            report['cases'][name] = result
            (work / 'checks.json').write_text(json.dumps(report, indent=2) + '\n')
            print(name + ': pass', flush=True)
        a, b = [report['cases'][name]['probes_V'] for name in ('four', 'refined')]
        report['maximum_refinement_V'] = max(abs(x - y) for ra, rb in zip(a, b) for x, y in zip(ra, rb))
        if report['maximum_refinement_V'] > .02:
            raise ValueError('precision receive refinement exceeds 20 mV')
        if any(digest(path) != value for path, value in (hashes | binaries).items()):
            raise ValueError('precision receive inputs changed during qualification')
        report['status'] = 'pass'
    except BaseException as error:
        report.update(status='fail', error=str(error))
        raise
    finally:
        (work / 'checks.json').write_text(json.dumps(report, indent=2) + '\n')


def compare(args):
    case_definitions = cases(args.load_profile)
    work = args.work.resolve()
    (work / 'comparison.json').unlink(missing_ok=True)
    reports = []
    for flow in ('cpp', 'veriloga'):
        report = json.loads((work / flow / 'checks.json').read_text())
        if report.get('status') != 'pass' or report.get('flow') != flow or set(report['cases']) != set(case_definitions):
            raise ValueError('both complete precision receive flows must pass independently')
        if (report.get('load_profile') != args.load_profile or report.get('case_definitions') !=
                {name: list(values) for name, values in case_definitions.items()}):
            raise ValueError('receive flows tested different acquisition load profiles')
        if any(digest(path) != value for path, value in (report['source_sha256'] | report['binary_sha256']).items()):
            raise ValueError('stale precision receive proof')
        for name, (count, _, step) in case_definitions.items():
            result = report['cases'][name]
            if digest(result['waveform']) != result['waveform_sha256']:
                raise ValueError('precision receive waveform changed')
            with gzip.open(result['waveform'], 'rt') as stream:
                if flow == 'cpp':
                    reader = csv.reader(stream)
                    if next(reader) != ['time_s', 'PLUS', 'MINUS', 'DRAIN', 'BUFFER', 'VDD', 'VLOGIC'] + [
                            'HELD' + str(i) for i in range(count)]:
                        raise ValueError('changed precision receive columns')
                    rows = checked_rows(reader, 7 + count, str(result['waveform']))
                else:
                    if len(next(stream).split()) != 7 + count:
                        raise ValueError('changed precision receive SPICE columns')
                    rows = checked_rows((line.split() for line in stream if line.strip()), 7 + count,
                                        str(result['waveform']))
            current = contract(rows, count, step)
            verify(current, name == 'overload')
            if any(result[key] != value for key, value in current.items()):
                raise ValueError('precision receive absolute evidence changed')
        reports.append(report)
    if reports[0]['source_sha256'] != reports[1]['source_sha256']:
        raise ValueError('precision receive flows tested different sources')
    differences = {}
    for name in case_definitions:
        a, b = [report['cases'][name] for report in reports]
        wave = max(abs(x - y) for ra, rb in zip(a['probes_V'], b['probes_V']) for x, y in zip(ra, rb))
        sample = max(abs(x - y) for x, y in zip(a['sample_V'], b['sample_V']))
        if wave > .02 or sample > .01:
            raise ValueError('precision receive solver disagreement: ' + name)
        differences[name] = dict(maximum_waveform_difference_V=wave, maximum_sample_difference_V=sample)
    (work / 'comparison.json').write_text(json.dumps(dict(status='pass', cases=differences,
        flow_report_sha256={flow: digest(work / flow / 'checks.json') for flow in ('cpp', 'veriloga')}), indent=2) + '\n')


if __name__ == '__main__':
    configure_cleanup()
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--flow', choices=('cpp', 'veriloga', 'compare'), required=True)
    parser.add_argument('--work', type=Path, required=True)
    parser.add_argument('--bench', type=Path)
    parser.add_argument('--fixture-source', type=Path, default=SOURCE.with_suffix('.cpp'))
    parser.add_argument('--openvaf', type=Path)
    parser.add_argument('--ngspice', type=Path)
    parser.add_argument('--load-profile', choices=('standard', 'parallel'), default='standard')
    parser.add_argument('--cxx', default='/usr/bin/c++')
    args = parser.parse_args()
    compare(args) if args.flow == 'compare' else run(args)
