#!/usr/bin/env python3
"""Finish a completed native run if its wrapper ended before writing checks."""
import argparse
import json
from pathlib import Path

from measure import measure
from provenance import digest, receipt, verify


def finish(binding, bench, work, step, degree, levels):
    binding, bench, work = (p.resolve() for p in (binding, bench, work))
    if (work/'checks.json').exists():
        raise ValueError('case already has a final receipt')
    hashes = json.loads((work/'inputs.json').read_text())
    verify(hashes)
    if hashes.get(str(bench)) != digest(bench):
        raise ValueError('executable differs from the launched case')
    for path in binding.iterdir():
        if path.is_file() and hashes.get(str(path.resolve())) != digest(path):
            raise ValueError('binding differs from the launched case: '+str(path))
    native = json.loads((work/'native.json').read_text())
    stop = 20e-9 + 50e-9*len(levels)
    if (native.get('engine') != 'Analog++ OSDI' or
        not native.get('complete') or not native.get('conversion_complete') or
        native.get('elimination_degree') != degree or
        abs(native.get('conversion_maximum_step_s', 0)-step) > 1e-20 or
        native.get('stop_s', 0) < stop-1e-18):
        raise ValueError('native run does not match the requested completed campaign')
    measured = measure(work)
    if [r['input_V'] for r in measured['samples']] != levels:
        raise ValueError('conversion inventory differs from requested levels')
    verify(hashes)
    command = [str(bench), '--degree', str(degree), str(binding/'circuit.txt'),
               str(work), str(step), *map(str, levels)]
    result = dict(status=measured['status'], command=command, qualified=False,
                  native=native, measurements=measured,
                  recovery_scope='Native output completed; wrapper interrupted before final receipt. '
                                 'Inputs and full accepted waveform independently rechecked.')
    (work/'checks.json').write_text(json.dumps(result, indent=2)+'\n')
    receipt(work/'manifest.json', hashes,
            [p for p in work.iterdir() if p.is_file() and p.name not in ('inputs.json', 'manifest.json')],
            status=result['status'], qualified=False)
    return result


if __name__ == '__main__':
    p = argparse.ArgumentParser(description=__doc__)
    for name in ('binding', 'bench', 'work'):
        p.add_argument('--'+name, type=Path, required=True)
    p.add_argument('--step', type=float, default=20e-12)
    p.add_argument('--degree', type=int, default=32)
    p.add_argument('--levels', type=float, nargs='+', required=True)
    a = p.parse_args()
    print(finish(a.binding, a.bench, a.work, a.step, a.degree, a.levels)['status'])
