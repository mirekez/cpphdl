#!/usr/bin/env python3
"""Compare completed matmul runs only when output and clock counts agree."""

import argparse
import hashlib
import json
import os
from pathlib import Path
import re
import statistics
import subprocess
import time


def parse_run(output, status):
    stats = re.findall(r"CVA6_BENCH reset_cycles=(\d+) work_cycles=(\d+) "
                       r"total_cycles=(\d+) work_seconds=([0-9.]+)", output)
    success = re.findall(r"\*\*\* SUCCESS \*\*\* \(tohost = 0\) after (\d+) cycles", output)
    result = dict(status=status, passed=False)
    if len(stats) != 1:
        result['reason'] = 'missing or ambiguous simulation statistics'
        return result
    reset, work, total, seconds = stats[0]
    result.update(reset_cycles=int(reset), work_cycles=int(work),
                  total_cycles=int(total), work_seconds=float(seconds))
    result['passed'] = (status == 0 and len(success) == 1
                        and re.search(r'^PASSED\r?$', output, re.MULTILINE) is not None
                        and int(success[0]) == int(total)
                        and int(reset) + int(work) == int(total)
                        and int(work) > 0 and float(seconds) > 0)
    if not result['passed']:
        result['reason'] = 'matmul did not pass with consistent clock accounting'
    return result


def equivalent(reference, candidate):
    return (reference['passed'] and candidate['passed']
            and all(reference[name] == candidate[name]
                    for name in ['reset_cycles', 'work_cycles', 'total_cycles']))


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--verilator', type=Path, required=True)
    parser.add_argument('--cpphdl', type=Path, required=True)
    parser.add_argument('--elf', type=Path, default=Path(__file__).with_name('matrix_multiply.riscv'))
    parser.add_argument('--output', type=Path, required=True)
    parser.add_argument('--max-cycles', type=int, default=100000)
    parser.add_argument('--trials', type=int, default=3)
    parser.add_argument('--timeout', type=float, default=600)
    parser.add_argument('--cpu', type=int)
    args = parser.parse_args()
    if args.max_cycles <= 10 or args.trials < 1 or args.timeout <= 0:
        parser.error('use more than 10 cycles, positive trials and a positive timeout')
    output = args.output.resolve()
    output.mkdir(parents=True, exist_ok=True)
    (output / 'comparison.json').unlink(missing_ok=True)
    environment = {name: value for name, value in os.environ.items()
                   if not name.startswith(('CPPHDL_', 'HDLCPP_', 'CVA6_', 'NATIVE_'))
                   and name != 'LD_PRELOAD'}
    environment['CVA6_BENCH_STATS'] = '1'
    binaries = dict(verilator=args.verilator.resolve(), cpphdl=args.cpphdl.resolve())
    elf = args.elf.resolve()
    report = dict(equivalent=False, elf=str(elf),
                  elf_sha256=hashlib.sha256(elf.read_bytes()).hexdigest(),
                  binaries={name: dict(path=str(path), sha256=hashlib.sha256(path.read_bytes()).hexdigest())
                            for name, path in binaries.items()}, runs=[])

    def save():
        (output / 'comparison.json').write_text(json.dumps(report, indent=2) + '\n')

    def run(variant, cycles, label):
        directory = output / (label + '-' + variant)
        directory.mkdir(exist_ok=True)
        command = ([] if args.cpu is None else ['taskset', '-c', str(args.cpu)])
        command += [str(binaries[variant])]
        command += (['--seed=1', '--max-cycles=' + str(cycles), str(elf)]
                    if variant == 'verilator' else [str(elf), str(cycles)])
        started = time.perf_counter()
        try:
            process = subprocess.run(command, cwd=directory, env=environment,
                                     stdout=subprocess.PIPE, stderr=subprocess.STDOUT,
                                     timeout=args.timeout)
            text = process.stdout.decode(errors='replace')
            result = parse_run(text, process.returncode)
        except subprocess.TimeoutExpired as error:
            text = (error.stdout or b'').decode(errors='replace')
            result = dict(status='host-timeout', passed=False, reason='host timeout')
        (directory / 'run.log').write_text(text)
        result.update(variant=variant, label=label, cap=cycles, command=command,
                      process_seconds=time.perf_counter() - started)
        report['runs'].append(result)
        save()
        print(json.dumps(result), flush=True)
        return result

    # Calibrate on successful RTL execution, not an arbitrary short timeout.
    # Both timed variants must complete the self-check at the same edge count.
    reference = run('verilator', args.max_cycles, 'validation')
    if not reference['passed']:
        report['reason'] = 'Verilator reference failed validation'
        save()
        return 1
    cycles = reference['total_cycles']
    candidate = run('cpphdl', cycles, 'validation')
    if not equivalent(reference, candidate):
        report['reason'] = 'outputs or simulated clock counts are not equivalent; no speed ratio'
        save()
        return 1

    measured = {name: [] for name in binaries}
    for trial in range(1, args.trials + 1):
        variants = list(binaries)
        if trial % 2 == 0:
            variants.reverse()
        for variant in variants:
            result = run(variant, cycles, 'trial-' + str(trial))
            if not equivalent(reference, result):
                report['reason'] = 'a timed run failed equivalence; no speed ratio'
                save()
                return 1
            measured[variant].append(result['work_seconds'])
    medians = {name: statistics.median(values) for name, values in measured.items()}
    report.update(equivalent=True, total_cycles=cycles, work_cycles=reference['work_cycles'],
                  median_work_seconds=medians,
                  cpphdl_over_verilator=medians['cpphdl'] / medians['verilator'])
    save()
    print(json.dumps({name: value for name, value in report.items() if name != 'runs'}), flush=True)
    return 0


if __name__ == '__main__':
    raise SystemExit(main())
