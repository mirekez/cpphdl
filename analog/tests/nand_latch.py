"""Check physical set/reset/hold/clear and loading independently in both solvers."""
import argparse
import bisect
import hashlib
import json
import math
from pathlib import Path
import subprocess
import sys

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / 'VerilogA'))
sys.path.insert(0, str(ROOT / '1G/tests'))
from compare import read_cpp, read_spice
from hardware import source_dependencies

parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument('--bench', type=Path, required=True)
parser.add_argument('--work', type=Path, required=True)
parser.add_argument('--openvaf', type=Path, required=True)
parser.add_argument('--ngspice', type=Path, required=True)
parser.add_argument('--fixture-source', type=Path, default=Path(__file__).with_suffix('.cpp'))
parser.add_argument('--cxx', default='/usr/bin/c++')
parser.add_argument('--step-ps', type=float, default=2)
args = parser.parse_args()
work = args.work.resolve()
work.mkdir(parents=True, exist_ok=True)
(work / 'checks.json').unlink(missing_ok=True)
report = {}
hashes = source_dependencies([args.fixture_source.resolve()], ROOT.parent, args.cxx,
    ['-std=c++20', '-I' + str(ROOT), '-I' + str(ROOT / 'include')])
hashes.update({str(path.resolve()): hashlib.sha256(path.read_bytes()).hexdigest()
               for path in (Path(__file__), ROOT / 'VerilogA/compare.py', args.bench, args.openvaf, args.ngspice)})

def run(directory, name, command):
    with (directory / (name + '.log')).open('w') as log:
        subprocess.run(list(map(str, command)), cwd=directory, stdout=log, stderr=-2, check=True, timeout=120)

for loaded in (0, 1):
    directory = work / str(loaded)
    directory.mkdir(exist_ok=True)
    run(directory, 'native', [args.bench.resolve(), directory, 'run', loaded, args.step_ps])
    run(directory, 'export', [args.bench.resolve(), directory, 'export', loaded, args.step_ps])
    run(directory, 'compile', [args.openvaf.resolve(), 'design.va', '-o', 'design.osdi'])
    run(directory, 'spice', [args.ngspice.resolve(), '-n', '-b', 'bench.cir'])
    header, cpp = read_cpp(directory / 'cpp/trace.csv')
    va = read_spice(directory / 'spice.dat', header)
    expected = [(2, 0, 1), (8, 1, 0), (12, 1, 0), (16, 0, 1), (20, 0, 1),
                (23, 1, 1), (25, 1, 0), (28, 1, 0), (31, 1, 0)]

    def interpolator(rows):
        times = [row[0] for row in rows]
        if (not rows or abs(times[-1] - 32e-9) > 1e-14
                or any(v is None or not math.isfinite(v) for row in rows for v in row)
                or max(b - a for a, b in zip(times, times[1:])) > 4e-12):
            raise ValueError('incomplete/nonfinite latch trajectory')
        def at(time, column):
            i = min(max(1, bisect.bisect_left(times, time)), len(times) - 1)
            a, b = rows[i - 1], rows[i]
            return a[column] + (b[column] - a[column]) * (time - a[0]) / (b[0] - a[0])
        return at

    readers = [interpolator(cpp), interpolator(va)]
    flows = {}
    for flow, rows, at in zip(('cpp', 'veriloga'), (cpp, va), readers):
        states = [[at(time * 1e-9, column) for column in (1, 2)] for time, _, _ in expected]
        for (time, q, qn), actual in zip(expected, states):
            if any(not (v > 2.8 if bit else v < .4) for bit, v in zip((q, qn), actual)):
                raise ValueError(f'{flow}, loaded={loaded}, truth/hold failed at {time} ns: {actual}')
        supply = min(row[3] for row in rows if row[0] > 1e-9)
        if not 3.0 < supply < 3.3:
            raise ValueError('latch did not operate from its finite loaded supply')
        flows[flow] = dict(states_V=states, minimum_supply_V=supply)
    rms = [math.sqrt(sum((readers[0](i * 4e-12, col) - readers[1](i * 4e-12, col)) ** 2
                        for i in range(8001)) / 8001) for col in (1, 2, 3)]
    if max(rms) > .02:
        raise ValueError('independently passing latch waveforms differ: ' + str(rms))
    report[str(loaded)] = dict(flows=flows, waveform_rms_V=rms,
        model_sha256=hashlib.sha256((directory / 'design.va').read_bytes()).hexdigest())
if any(hashlib.sha256(Path(path).read_bytes()).hexdigest() != value for path, value in hashes.items()):
    raise ValueError('latch qualification inputs changed')
(work / 'checks.json').write_text(json.dumps(dict(status='pass', cases=report, source_sha256=hashes,
                                                step_ps=args.step_ps), indent=2) + '\n')
print('PASS electrical NAND latch: clear, set, reset, hold, both asserted, recovery and output loading in both solvers')
