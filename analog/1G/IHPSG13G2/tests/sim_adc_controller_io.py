#!/usr/bin/env python3
"""A failed CSV write must stop co-simulation immediately with a clear error."""
import argparse
import csv
import json
from pathlib import Path
import sys

SOURCE = Path(__file__).resolve()
ANALOG = SOURCE.parents[3]
sys.path[:0] = [str(SOURCE.parents[1]), str(ANALOG / '1G/tests'), str(ANALOG / 'VerilogA')]
from hardware import execute, source_dependencies
from transmit_driver import digest
from xspice_live import headers


def run(args):
    work = args.work.resolve()
    work.mkdir(parents=True, exist_ok=True)
    fixture = SOURCE.with_suffix('.cpp')
    include = headers(args.ngspice)
    defines = ['-DCPPHDL_ADC_COMPACT'] if args.compact else []
    hashes = source_dependencies([fixture], ANALOG.parent, args.cxx, ['-I' + str(include), *defines])
    hashes[str(SOURCE)] = digest(SOURCE)
    binary = work / 'controller-io'
    with (work / 'compile.log').open('w') as log:
        execute([args.cxx, '-std=c++17', '-O1', *defines, '-I' + str(ANALOG.parent / 'include'), '-I' + str(include),
                 str(fixture), '-o', str(binary)], stdout=log, stderr=-2, check=True, timeout=120,
                memory_exclusive=False)
    cases = [('normal', work / 'controls.csv', work / 'events.csv', None),
             ('controls_full', Path('/dev/full'), work / 'events-controls-full.csv', 'cannot write XSPICE controller evidence'),
             ('events_full', work / 'controls-events-full.csv', Path('/dev/full'), 'cannot write XSPICE bridge-event evidence')]
    results = {}
    for name, controls, events, expected in cases:
        logfile = work / (name + '.log')
        with logfile.open('w') as log:
            result = execute([str(binary), str(controls), str(events), str(args.slices)], stdout=log, stderr=-2, timeout=10)
        message = logfile.read_text()
        if expected is None:
            if result.returncode != 0:
                raise ValueError('normal adapter evidence failed: ' + message)
            for path, indices in [(controls, [-1, 0, 1]), (events, [0, 1])]:
                with path.open() as file:
                    rows = list(csv.DictReader(file))
                if [int(row['clock']) for row in rows] != indices:
                    raise ValueError('adapter evidence lost a normal clock')
                if path == controls and (len(rows[0]) != args.slices + 11 or
                        not all('trial' + str(i) in rows[0] for i in range(args.slices))):
                    raise ValueError('adapter evidence lost a physical slice')
        elif result.returncode != 1 or expected not in message:
            raise ValueError('write failure was not reported immediately: ' + name)
        results[name] = dict(returncode=result.returncode, expected_error=expected)
    if any(digest(Path(path)) != value for path, value in hashes.items()):
        raise ValueError('adapter inputs changed during verification')
    (work / 'checks.json').write_text(json.dumps(dict(status='pass', cases=results, source_sha256=hashes, compact=args.compact, slices=args.slices,
        scope='real adapter ABI and file-write failure handling; not analog or ngspice bridge qualification'), indent=2) + '\n')
    print('PASS normal capture and both immediate CSV write failures')


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--work', type=Path, required=True)
    parser.add_argument('--ngspice', type=Path, required=True)
    parser.add_argument('--cxx', default='/usr/bin/c++')
    parser.add_argument('--compact', action='store_true')
    parser.add_argument('--slices', type=int, default=1)
    args = parser.parse_args()
    if args.slices not in ((1, 4, 13) if args.compact else (1, 4, 16)):
        parser.error('slice count does not match the compiled controller bank')
    run(args)
