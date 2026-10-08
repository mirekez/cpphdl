#!/usr/bin/env python3
"""Measure all run scripts sequentially without rebuilding their binaries."""
import csv
import datetime
import hashlib
import json
import os
from pathlib import Path
import platform
import re
import subprocess
import time

out = Path(__file__).resolve().parent
root = out.parents[1]
chipyard = (root / 'chipyard').resolve()
elf = chipyard / 'tests/build/rocket64-mmul.riscv'
expected = '0xe49d58d75696cd28'
sha = lambda path: hashlib.sha256(path.read_bytes()).hexdigest()
previous = json.loads((out / 'previous-summary.json').read_text())
env = dict(os.environ)
for key in ('CPPHDL_BUILD_DIR', 'CPPHDL_OUTPUT_DIR', 'CPPHDL_TEST_LOG', 'LD_PRELOAD', 'CPU_PROFILE_OUT'):
    env.pop(key, None)
env.update(CPPHDL_TIMEOUT='1200s', RV64_TIMEOUT='1200s',
           CPPHDL_MAX_CYCLES='2000000', RV64_MAX_CYCLES='100000000',
           CPPHDL_PROGRESS_CYCLES='100000')
metadata = dict(started=datetime.datetime.now(datetime.timezone.utc).isoformat(),
                host=platform.platform(), cpu_count=os.cpu_count(), elf=str(elf), elf_sha256=sha(elf),
                workload='16x16 matrix, 8 rounds', sequential=True, repetitions=1,
                notes='Fresh uninstrumented run scripts; previous build timings retained with their original scopes.')
(out / 'metadata.json').write_text(json.dumps(metadata, indent=2) + '\n')
results = []
for old in previous:
    backend = old['backend']
    binary = Path(old['binary']).resolve()
    assert binary.is_file() and os.access(binary, os.X_OK), binary
    command = ['./.run_rocket64_native.sh'] if backend == 'verilator' else ['./.run_rocket64_cpphdl.sh', backend]
    # Resolve the public graph link too, so the measured script cannot silently
    # select an artifact different from the accepted runner in the table.
    if backend == 'native-graph':
        assert (chipyard / 'cpphdl-build/RocketConfig/native-graph/runtime/cpphdl-rocket64-graph-sim').resolve() == binary
    binary_hash = sha(binary)
    log, timing = out / (backend + '.log'), out / (backend + '.time')
    started = datetime.datetime.now(datetime.timezone.utc).isoformat()
    print(f'START {backend} {started}', flush=True)
    before = time.monotonic()
    with log.open('w') as stream:
        status = subprocess.run(['/usr/bin/time', '-v', '-o', str(timing), *command], cwd=root,
                                env=dict(env, CPPHDL_TEST_LOG=str(out / (backend + '-simulator.log'))),
                                stdin=subprocess.DEVNULL, stdout=stream, stderr=subprocess.STDOUT).returncode
    seconds = time.monotonic() - before
    text = log.read_text()
    signatures = re.findall(r'ROCKET RV64 MMUL TEST PASSED: signature=(0x[0-9a-fA-F]+)', text)
    cycles = re.findall(r'simulation finished after (\d+) cycles', text)
    unchanged = sha(binary) == binary_hash and sha(elf) == metadata['elf_sha256']
    passed = status == 0 and bool(signatures) and all(s == expected for s in signatures) and unchanged
    timer = timing.read_text()
    user = float(re.search(r'User time \(seconds\): ([\d.]+)', timer)[1])
    system = float(re.search(r'System time \(seconds\): ([\d.]+)', timer)[1])
    record = dict(old)
    record.update(command=command, started=started, run_seconds=seconds, cpu_seconds=user+system,
                  exit_code=status, result='PASS' if passed else 'FAIL',
                  cycles=int(cycles[-1]) if cycles else None,
                  signature=signatures[-1] if signatures else None, build_repeated=False,
                  binary=str(binary), binary_sha256=binary_hash, elf_sha256=metadata['elf_sha256'],
                  artifacts_unchanged=unchanged, log=str(log), timing=str(timing),
                  measurement_source=str(out / 'summary.json'), measurement_date=started[:10],
                  run_aggregation='single uninstrumented run via public run script',
                  table_note='Fresh run; build time retained from prior measurement with its original scope')
    results.append(record)
    (out / 'summary.json').write_text(json.dumps(results, indent=2) + '\n')
    with (out / 'summary.tsv').open('w') as stream:
        writer = csv.writer(stream, delimiter='\t', lineterminator='\n')
        writer.writerow(['backend','previous_build_seconds','build_scope','new_run_seconds','cycles','signature','result'])
        for row in results:
            writer.writerow([row['backend'],f"{row['build_seconds']:.3f}",row['build_scope'],
                             f"{row['run_seconds']:.3f}",row['cycles'] or '',row['signature'] or '',row['result']])
    print(f"DONE {backend} {seconds:.3f}s wall {user+system:.3f}s CPU {record['result']}", flush=True)
    if not passed:
        raise RuntimeError(f'{backend} failed: {log}')
print('All four run scripts passed.', flush=True)
