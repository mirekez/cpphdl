"""Recheck independent generic conversion flows before comparing ADC codes."""
import argparse
import csv
import json
from pathlib import Path
from differential_receive_conversion import contract, read_cpp, read_spice, digest, SOURCE as CHECKER


def require_same_profile(first, second):
    keys = ('slices', 'gain', 'storage_pf', 'bursts', 'fixture_source', 'gain_compensation_F', 'corner', 'temperature_C')
    if (any(key not in first or key not in second or first[key] != second[key] for key in keys)
            or not first.get('fixture_source')):
        raise ValueError('conversion flows used different or incomplete circuit profiles')


def compare(work):
    output = work / 'comparison.json'
    output.unlink(missing_ok=True)
    reports = []
    for flow in ('cpp', 'veriloga'):
        path = work / flow / 'checks.json'
        report = json.loads(path.read_text())
        if report.get('status') != 'pass' or report.get('flow') != flow:
            raise ValueError('comparison requires independently passing conversion flows')
        fixture = report.get('fixture_source')
        if not fixture or not {str(CHECKER), fixture} <= report.get('source_sha256', {}).keys():
            raise ValueError('missing conversion provenance')
        for name, expected in report['source_sha256'].items():
            if digest(name) != expected:
                raise ValueError('stale conversion source or tool: ' + name)
        for name, expected in report['artifact_sha256'].items():
            if digest(name) != expected:
                raise ValueError('changed conversion artifact: ' + name)
        case = Path(report['work'])
        raw = Path(report['waveform'])
        if digest(raw) != report['waveform_sha256']:
            raise ValueError('changed conversion waveform')
        if flow == 'cpp':
            header, rows = read_cpp(raw)
        else:
            header = next(csv.reader((case / 'columns.csv').open()))
            rows = read_spice(raw, header)
        with (case / 'controls.csv').open() as source:
            controls = [{k: int(v) for k, v in row.items()} for row in csv.DictReader(source)]
        current = contract(header, rows, controls, json.loads((case / 'live.json').read_text()))
        if not all(current['checks'].values()) or any(report.get(k) != v for k, v in current.items()):
            raise ValueError('conversion evidence no longer passes its electrical contract')
        reports.append(report)
    first, second = reports
    require_same_profile(first, second)
    for path in first['source_sha256'].keys() & second['source_sha256'].keys():
        if first['source_sha256'][path] != second['source_sha256'][path]:
            raise ValueError('conversion flows used different common sources')
    differences = [abs(a - b) for a, b in zip(first['codes'], second['codes'])]
    if len(differences) != first['slices'] * first['bursts'] or max(differences) > 1:
        raise ValueError('generic conversion flows differ by more than one ADC count')
    result = dict(status='pass', code_differences=differences,
                  comparison_source_sha256={str(Path(__file__).resolve()): digest(Path(__file__))},
                  flow_report_sha256={flow: digest(work / flow / 'checks.json') for flow in ('cpp', 'veriloga')},
                  scope='separately checked closed conversion, including declared bench bridge timing; one-count generic code tolerance; PSP has a separate device law and purpose check')
    output.write_text(json.dumps(result, indent=2) + '\n')
    print('PASS independently checked generic conversion comparison:', differences)


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--work', type=Path, required=True)
    args = parser.parse_args()
    compare(args.work.resolve())
