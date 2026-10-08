#!/usr/bin/env python3
"""Alternating full-workload runs; report incremental runtime reductions."""
import argparse
import hashlib
import json
import os
from pathlib import Path
import resource
import statistics
import subprocess
import time

p = argparse.ArgumentParser()
p.add_argument('--baseline', required=True, type=Path)
p.add_argument('--candidate', required=True, type=Path)
p.add_argument('--label', required=True)
p.add_argument('--pairs', type=int, default=3)
args = p.parse_args()
out = Path(__file__).resolve().parent / args.label
out.mkdir(exist_ok=True)
elf = Path('/home/me/chipyard/chipyard/tests/build/rocket64-mmul.riscv')
binaries = dict(baseline=args.baseline.resolve(), candidate=args.candidate.resolve())
sha = lambda path: hashlib.sha256(path.read_bytes()).hexdigest()
hashes = {k: sha(v) for k, v in binaries.items()}
elf_sha = sha(elf)
env = dict(os.environ, CPPHDL_MAX_CYCLES='2000000', CPPHDL_PROGRESS_CYCLES='100000')
env.pop('LD_PRELOAD', None)
rows = []
for pair in range(args.pairs):
    for key in (('baseline', 'candidate') if pair % 2 == 0 else ('candidate', 'baseline')):
        log = out / f'{pair+1}-{key}.log'
        print(f'START {args.label} pair {pair+1} {key}', flush=True)
        before = resource.getrusage(resource.RUSAGE_CHILDREN)
        start = time.monotonic()
        with log.open('w') as stream:
            proc = subprocess.run([str(binaries[key]), str(elf)], env=env,
                                  stdin=subprocess.DEVNULL, stdout=stream, stderr=subprocess.STDOUT, timeout=1200)
        wall = time.monotonic() - start
        after = resource.getrusage(resource.RUSAGE_CHILDREN)
        cpu = after.ru_utime + after.ru_stime - before.ru_utime - before.ru_stime
        text = log.read_text()
        passed = (proc.returncode == 0 and 'ROCKET RV64 MMUL TEST PASSED: signature=0xe49d58d75696cd28' in text
                  and 'finished after 695870 cycles (code 0)' in text)
        assert sha(binaries[key]) == hashes[key] and sha(elf) == elf_sha, 'binary changed during benchmark'
        rows.append(dict(pair=pair+1, variant=key, seconds=wall, cpu_seconds=cpu, passed=passed))
        (out/'runs.json').write_text(json.dumps(rows, indent=2)+'\n')
        print(f'DONE {wall:.3f}s wall {cpu:.3f}s CPU {"PASS" if passed else "FAIL"}', flush=True)
        if not passed:
            raise RuntimeError(f'Correctness failure: {log}')
medians = {key: {metric: statistics.median(row[metric] for row in rows if row['variant'] == key)
                 for metric in ('seconds', 'cpu_seconds')} for key in binaries}
improvements = {metric: 100*(1-medians['candidate'][metric]/medians['baseline'][metric])
                for metric in ('seconds', 'cpu_seconds')}
result = dict(binaries={k:str(v) for k,v in binaries.items()}, hashes=hashes, elf_sha256=elf_sha,
              runs=rows, medians=medians, reduction_percent=improvements,
              accepted=all(x>10 for x in improvements.values()))
(out/'results.json').write_text(json.dumps(result, indent=2)+'\n')
print(json.dumps(result, indent=2), flush=True)
