#!/usr/bin/env python3
"""Audit a real SPICE reference and compare complete native/ngspice waveforms.

This is a comparison gate, not a PSP implementation or a replacement simulator.
The strict comparison requires the same sample times and all requested columns.
It never interpolates, decimates, or supplies reference values to a native model.
"""
import argparse
from collections import Counter
import gzip
import hashlib
import json
import math
from pathlib import Path
import re
import shlex
import stat
import struct


def digest(path):
    if not stat.S_ISREG(Path(path).stat().st_mode):
        raise ValueError('input must be a regular file: ' + str(path))
    h = hashlib.sha256()
    with Path(path).open('rb') as stream:
        for block in iter(lambda: stream.read(1024 * 1024), b''):
            h.update(block)
    return h.hexdigest()


def logical_lines(path):
    pending = ''
    for line in Path(path).read_text().splitlines():
        line = line.strip()
        if not line or line.startswith('*'):
            continue
        if line.startswith('+'):
            if not pending:
                raise ValueError(f'{path}: orphan SPICE continuation')
            pending += ' ' + line[1:].strip()
        else:
            if pending:
                yield pending
            pending = line
    if pending:
        yield pending


def audit(deck, model_binaries=()):
    """Read selected libraries and all their dependencies, without executing SPICE.

    Conditional device branches remain unexpanded. Counts are lexical declarations,
    never physical instance totals; expressions and conditionals require a frontend.
    """
    deck = Path(deck).resolve()
    hashes, visited, counts, directives = {}, set(), Counter(), Counter()
    models, observations, controls, libraries, statements = {}, [], [], [], []
    features = set()

    def visit(path, section=None, chain=()):
        path = path.resolve()
        key = (path, section)
        if key in chain:
            raise ValueError(f'recursive model include: {path}')
        if key in visited:
            return
        visited.add(key)
        hashes[str(path)] = digest(path)
        active, found, control = section is None, section is None, False
        for line in logical_lines(path):
            words = shlex.split(line, posix=True)
            command = words[0].lower()
            if command == '.lib' and len(words) == 2:
                active = section is not None and words[1].lower() == section.lower()
                found |= active
                continue
            if command in ('.endl', '.endlibrary'):
                active = section is None
                continue
            if not active:
                continue
            if command in ('.include', '.inc') or (command == '.lib' and len(words) == 3):
                included = Path(words[1])
                if not included.is_absolute():
                    included = path.parent / included
                selected = words[2] if command == '.lib' else None
                libraries.append({'file': str(included.resolve()), 'section': selected})
                visit(included, selected, chain + (key,))
                continue
            if command == '.control':
                control = True
                continue
            if command == '.endc':
                control = False
                continue
            if control:
                controls.append(line)
                if command == 'let':
                    match = re.fullmatch(r'let\s+(\w+)\s*=\s*(.+)', line, re.I)
                    if not match:
                        raise ValueError('unsupported observation declaration: ' + line)
                    observations.append({'name': match[1], 'expression': match[2]})
                continue
            if command.startswith('.'):
                directives[command] += 1
                if command in ('.temp', '.options', '.option', '.ic', '.nodeset'):
                    statements.append({'file': str(path), 'statement': line})
                if command == '.model':
                    models[words[1]] = words[2].split('(')[0]
                continue
            if path == deck or words[0][0].lower() in 'rcvibmnxldq':
                counts[words[0][0].upper()] += 1
            if re.search(r'\bpwl\s*\(', line, re.I): features.add('pwl_sources')
            if re.search(r'\b(gauss|agauss|unif|aunif)\s*\(', line, re.I): features.add('statistical_expressions')
        if not found:
            raise ValueError(f'missing library section {section} in {path}')

    visit(deck)
    for path in model_binaries:
        path = Path(path).resolve()
        hashes[str(path)] = digest(path)
    if counts['N']: features.add('osdi_compact_models')
    if counts['B']: features.add('behavioral_sources')
    if counts['X']: features.add('hierarchical_subcircuits')
    if directives['.param']: features.add('parameter_expressions')
    if directives['.if']: features.add('conditional_model_elaboration')
    if directives['.func']: features.add('user_functions')
    if directives['.temp']: features.add('temperature')
    if counts['C'] or counts['N']: features.add('charge_integration')
    if counts['D']: features.add('junction_diode_models')
    if counts['L']: features.add('inductors')
    if any(re.search(r'\buic\b', command, re.I) for command in controls): features.add('uic_initialization')
    # Every transient reference also requires its timestep/error and convergence
    # semantics, even when the syntax resembles an already supported C++ method.
    if any(command.lower().startswith('tran ') for command in controls):
        features.update(('ngspice_transient_error_control', 'ngspice_convergence'))
    return dict(schema=1, deck=str(deck), input_sha256=hashes, libraries=libraries,
                lexical_device_declarations=dict(counts), directives=dict(directives),
                model_types=models, observations=observations, control=controls, analysis_statements=statements,
                required_features=sorted(features),
                count_scope='unique source declarations; conditional branches and hierarchy are not elaborated',
                native_full_deck_supported=False, full_1g_qualified=False)


def rows(path, columns):
    path = Path(path)
    if not stat.S_ISREG(path.stat().st_mode):
        raise ValueError('reference/candidate must be a completed regular file, not a FIFO')
    opener = gzip.open if path.suffix == '.gz' else open
    with opener(path, 'rt') as stream:
        header = stream.readline().split()
        canonical = list(columns)
        spice = ['time'] + [f'observation{i}' for i in range(len(columns) - 1)]
        if header not in (canonical, spice):
            raise ValueError('trace column names/order differ from the reference contract')
        previous = -math.inf
        for line_number, line in enumerate(stream, 2):
            values = [float(value) for value in line.split()]
            if len(values) != len(columns) or not all(map(math.isfinite, values)):
                raise ValueError(f'incomplete/nonfinite trace row {line_number}')
            if values[0] < 0 or values[0] < previous:
                raise ValueError(f'nonmonotonic time at row {line_number}')
            previous = values[0]
            yield values


def compare(reference, candidate, columns, stop, absolute=0, relative=0):
    if (len(columns) < 2 or len(set(columns)) != len(columns) or columns[0] != 'time_s' or
            not math.isfinite(stop) or stop <= 0 or
            not math.isfinite(absolute + relative) or min(absolute, relative) < 0):
        raise ValueError('invalid waveform contract')
    left, right = rows(reference, columns), rows(candidate, columns)
    count, failures, identical = 0, 0, True
    maximum = [0.0] * (len(columns) - 1)
    first, last = None, None
    while True:
        a, b = next(left, None), next(right, None)
        if a is None and b is None:
            break
        if a is None or b is None:
            raise ValueError('trace lengths differ; complete output is required')
        if a[0] != b[0]:
            raise ValueError('sample times differ; strict comparison does not interpolate')
        if first is None:
            first = a[0]
        last = a[0]
        count += 1
        for i, (expected, actual) in enumerate(zip(a[1:], b[1:])):
            error = abs(actual - expected)
            maximum[i] = max(maximum[i], error)
            failures += error > absolute + relative * max(abs(expected), abs(actual))
            identical &= struct.pack('d', expected) == struct.pack('d', actual)
    if count < 2 or first > min(stop, 1e-12) or last != stop:
        raise ValueError('incomplete startup or stop-time coverage')
    return dict(status='pass' if not failures else 'fail', rows=count, failed_values=failures,
                bitwise_identical=identical, maximum_absolute_error=dict(zip(columns[1:], maximum)),
                absolute_tolerance=absolute, relative_tolerance=relative,
                reference_sha256=digest(reference), candidate_sha256=digest(candidate),
                scope='all retained waveform values at identical sample times; no interpolation')


def qualify(reference_receipt, candidate_receipt):
    """Accept measured speed only after independently checked complete runs agree.

    Receipts are produced by the simulation runner, not by this comparator.
    Each binds the full trace and all circuit/model inputs to content hashes.
    The future native PSP runner must produce this contract before replacement.
    """
    receipts = [json.loads(Path(path).read_text()) for path in (reference_receipt, candidate_receipt)]
    for run, engine in zip(receipts, ('ngspice', 'cpphdl')):
        if run['engine'] != engine or run['status'] != 'complete':
            raise ValueError('both simulation engines must complete successfully')
        if not run['input_sha256'] or not run['checks'] or not all(value is True for value in run['checks'].values()):
            raise ValueError('each output must independently pass the electrical checks')
        for name, expected in run['input_sha256'].items():
            if not Path(name).is_absolute():
                raise ValueError('receipt input paths must be absolute')
            if digest(name) != expected:
                raise ValueError('circuit/model input changed: ' + name)
        if not Path(run['trace']).is_absolute() or digest(run['trace']) != run['trace_sha256']:
            raise ValueError('completed trace hash does not match its receipt')
        timing = run['timing']
        if (timing['scope'] != 'simulation_and_output' or timing['threads'] != 1 or
                not math.isfinite(timing['wall_s']) or timing['wall_s'] <= 0):
            raise ValueError('require finite measured wall time, including output, with one thread')
    reference, candidate = receipts
    if Path(reference['trace']).samefile(candidate['trace']):
        raise ValueError('reference and candidate must be separate simulator outputs')
    for key in ('input_sha256', 'columns', 'stop_s', 'numerical_contract', 'host'):
        if not reference[key] or reference[key] != candidate[key]:
            raise ValueError('runs do not share the same ' + key)
    if set(reference['checks']) != set(candidate['checks']):
        raise ValueError('engines were not subjected to the same independent checks')
    result = compare(reference['trace'], candidate['trace'], reference['columns'], reference['stop_s'])
    speedup = reference['timing']['wall_s'] / candidate['timing']['wall_s']
    result.update(speedup=speedup, speed_target=100, speed_target_met=speedup >= 100,
                  reference_receipt_sha256=digest(reference_receipt),
                  candidate_receipt_sha256=digest(candidate_receipt))
    if result['status'] != 'pass' or not result['bitwise_identical'] or speedup < 100:
        result['status'] = 'fail'
    return result


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    sub = parser.add_subparsers(dest='command', required=True)
    scan = sub.add_parser('audit')
    scan.add_argument('--deck', type=Path, required=True)
    scan.add_argument('--osdi', type=Path, action='append', default=[])
    check = sub.add_parser('compare')
    for name in ('reference', 'candidate', 'columns'):
        check.add_argument('--' + name, type=Path, required=True)
    check.add_argument('--stop', type=float, required=True)
    check.add_argument('--absolute', type=float, default=0)
    check.add_argument('--relative', type=float, default=0)
    system = sub.add_parser('system')
    system.add_argument('--reference-receipt', type=Path, required=True)
    system.add_argument('--candidate-receipt', type=Path, required=True)
    parser.add_argument('--output', type=Path, required=True)
    args = parser.parse_args()
    if args.command == 'audit':
        result = audit(args.deck, args.osdi)
    elif args.command == 'system':
        result = qualify(args.reference_receipt, args.candidate_receipt)
    else:
        result = compare(args.reference, args.candidate, json.loads(args.columns.read_text()),
                         args.stop, args.absolute, args.relative)
    args.output.parent.mkdir(parents=True, exist_ok=True)
    args.output.write_text(json.dumps(result, indent=2) + '\n')
    if result.get('status') == 'fail':
        raise SystemExit(1)


if __name__ == '__main__':
    main()
