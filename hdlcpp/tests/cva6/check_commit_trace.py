#!/usr/bin/env python3
"""Compare integer architectural commits from Verilator and the CVA6 C++ runner."""

import argparse
import hashlib
import json
from pathlib import Path
import re


def verilator_records(text):
    records = []
    pattern = re.compile(r'^[0-3] 0x([0-9a-fA-F]+) \(0x([0-9a-fA-F]+)\)(.*)$')
    for line in text.splitlines():
        match = pattern.match(line)
        if match is None:
            continue
        register = re.search(r' x\s*(\d+) 0x([0-9a-fA-F]+)', match[3])
        destination = int(register[1]) if register else 0
        value = int(register[2], 16) if destination else 0
        records.append((int(match[1], 16), int(match[2], 16), destination, value))
    return records


def cpphdl_records(text):
    records = []
    pattern = re.compile(
        r'CPPCOMMIT cycle=\d+ ordinal=(\d+) lane=\d+ order=\d+ '
        r'pc=0x([0-9a-fA-F]+) next=0x[0-9a-fA-F]+ insn=0x([0-9a-fA-F]+) '
        r'trap=(\d+) cause=0x[0-9a-fA-F]+ rd=x(\d+) data=0x([0-9a-fA-F]+)')
    for line in text.splitlines():
        if not line.startswith('CPPCOMMIT '):
            continue
        match = pattern.fullmatch(line)
        if match is None:
            raise ValueError('malformed native commit: ' + line)
        if int(match[1]) != len(records) + 1:
            raise ValueError('nonconsecutive native commit ordinal: ' + line)
        if int(match[4]):
            raise ValueError('unexpected guest trap: ' + line)
        destination = int(match[5])
        if destination > 31:
            raise ValueError('invalid integer destination register: ' + line)
        records.append((int(match[2], 16), int(match[3], 16), destination,
                        int(match[6], 16) if destination else 0))
    return records


def compare_records(reference, candidate):
    matched = 0
    for expected, actual in zip(reference, candidate):
        if actual != expected:
            break
        matched += 1
    equivalent = bool(reference) and bool(candidate) and reference == candidate
    return dict(equivalent=equivalent, matched_prefix=matched,
                verilator_commits=len(reference), cpphdl_commits=len(candidate),
                first_difference=None if equivalent else dict(
                    ordinal=matched + 1,
                    verilator=reference[matched] if matched < len(reference) else None,
                    cpphdl=candidate[matched] if matched < len(candidate) else None))


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--verilator', type=Path, required=True)
    parser.add_argument('--cpphdl', type=Path, required=True)
    parser.add_argument('--output', type=Path, required=True)
    args = parser.parse_args()
    report = dict(equivalent=False)
    try:
        report = compare_records(verilator_records(args.verilator.read_text()),
                                 cpphdl_records(args.cpphdl.read_text()))
    except ValueError as error:
        report['reason'] = str(error)
    report['trace_sha256'] = {
        str(path): hashlib.sha256(path.read_bytes()).hexdigest()
        for path in (args.verilator, args.cpphdl)}
    args.output.write_text(json.dumps(report, indent=2) + '\n')
    print(json.dumps(report, indent=2))
    return 0 if report['equivalent'] else 1


if __name__ == '__main__':
    raise SystemExit(main())
