#!/usr/bin/env python3
"""Check source truth, slips and 42-symbol PCS latency from immutable traces."""
import argparse
import gzip
import json
from pathlib import Path


def audit(path, fixture_directory):
    source_words = []
    for role in (0, 1):
        tokens = list(map(int, (fixture_directory / f'role{role}.txt').read_text().split()))
        master, count, frames = tokens[:3]
        if master != role or frames != 2:
            raise ValueError('bad independent frame fixture')
        offset, source_index, words = 3 + count, 20000, {}
        for _ in range(frames):
            size = tokens[offset]
            offset += 1
            for byte in tokens[offset:offset + size]:
                words[source_index] = 256 | byte
                source_index += 1
            offset += size
            source_index += 24
        source_words.append(words)
    opener = gzip.open if path.suffix == '.gz' else open
    cases, current, latest, previous_tag, unwrapped = [], -1, None, 65520, 0
    with opener(path, 'rt') as trace:
        for line in trace:
            case, clock, word, tag, valid, pma, reset, output, output_valid, fault, locked, symbol_valid, symbol_tag, fraction = map(int, line.split(','))
            if case != current:
                current, latest, previous_tag, unwrapped = case, None, 65520, 0
                cases.append(dict(case=case, words=0, data_bytes=0, recovered_symbols=0, maximum_phase_UI=0))
            result = cases[-1]
            role, rate, delay_index = case // 9, (case % 9) // 3, case % 3
            period, delay = 1 + (-1500, 0, 1500)[rate] * 1e-6, (0, .105, .9)[delay_index]
            # Trace observes pre-edge outputs. They belong to the latest
            # previously accepted recovered symbol, never this edge's input.
            if output_valid:
                if latest is None or output != source_words[role].get(latest - 42, 0):
                    raise ValueError(f'case {case} clock {clock}: source/42-symbol latency mismatch')
                result['words'] += 1
                result['data_bytes'] += bool(output & 256)
            if symbol_valid:
                unwrapped += (symbol_tag - previous_tag) & 65535
                previous_tag = symbol_tag
                coordinate = (20 + (unwrapped + fraction / 2**24) * .5 + delay) / period - .65
                symbol = round(coordinate)
                if pma:
                    if latest is not None and symbol != latest + 1:
                        raise ValueError('source symbol slip after declared timing acquisition')
                    phase = abs(coordinate - symbol)
                    if phase >= .20:
                        raise ValueError('source-truth phase exceeded existing 0.20 UI bound')
                    result['maximum_phase_UI'] = max(result['maximum_phase_UI'], phase)
                    result['recovered_symbols'] += 1
                latest = symbol
    if len(cases) != 18 or any(c['data_bytes'] != 1598 or c['words'] < 4000 for c in cases):
        raise ValueError('missing decoded frame bytes or latency coverage')
    return dict(status='pass', pcs_latency_accepted_symbols=42, cases=cases)


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('trace', type=Path)
    parser.add_argument('fixture_directory', type=Path)
    args = parser.parse_args()
    print(json.dumps(audit(args.trace, args.fixture_directory), indent=2))
