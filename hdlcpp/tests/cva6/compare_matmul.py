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

from check_commit_trace import compare_records, cpphdl_records, verilator_records


def parse_run(output, status, require_graph_edges=False):
    stats = re.findall(r"CVA6_BENCH reset_cycles=(\d+) work_cycles=(\d+) "
                       r"total_cycles=(\d+) work_seconds=([0-9.]+)", output)
    success = re.findall(r"\*\*\* SUCCESS \*\*\* \(tohost = 0\) after (\d+) cycles", output)
    result = dict(status=status, passed=False)
    workload_lines = re.findall(r'^CVA6_MMUL .*$', output, re.MULTILINE)
    if workload_lines:
        workload = re.fullmatch(r'CVA6_MMUL size=(\d+) repeats=(\d+) macs=(\d+) '
                                r'checksum=0x([0-9a-fA-F]{8})\r?', workload_lines[0])
        if len(workload_lines) != 1 or workload is None:
            result['reason'] = 'missing or ambiguous large matmul workload signature'
            return result
        size, repeats, macs, checksum = workload.groups()
        if not (1 <= int(size) <= 128 and 1 <= int(repeats) <= 1024
                and int(macs) == int(size)**3 * int(repeats)):
            result['reason'] = 'inconsistent large matmul workload signature'
            return result
        result['workload'] = dict(size=int(size), repeats=int(repeats), macs=int(macs),
                                  checksum=int(checksum, 16))
    if len(stats) != 1:
        result['reason'] = 'missing or ambiguous simulation statistics'
        return result
    reset, work, total, seconds = stats[0]
    result.update(reset_cycles=int(reset), work_cycles=int(work),
                  total_cycles=int(total), work_seconds=float(seconds))
    edges = re.findall(r'CVA6_GRAPH_EDGES total_cycles=(\d+)', output)
    valid_edges = ((not require_graph_edges and not edges)
                   or (len(edges) == 1 and int(edges[0]) == int(total)))
    if len(edges) == 1:
        result['graph_edge_cycles'] = int(edges[0])
    result['passed'] = (status == 0 and len(success) == 1
                        and valid_edges
                        and len(re.findall(r'^PASSED\r?$', output, re.MULTILINE)) == 1
                        and int(success[0]) == int(total)
                        and int(reset) + int(work) == int(total)
                        and int(work) > 0 and float(seconds) > 0)
    if not result['passed']:
        result['reason'] = 'matmul did not pass with consistent clock accounting'
    return result


def equivalent(reference, candidate):
    return (reference['passed'] and candidate['passed']
            and reference.get('workload') == candidate.get('workload')
            and all(reference[name] == candidate[name]
                    for name in ['reset_cycles', 'work_cycles', 'total_cycles']))


def simulation_command(binary, variant, elf, cycles):
    if variant == 'verilator':
        # Match cpphdl's ELF-derived tracer termination instead of waiting for
        # a later DTM poll. The native driver accepts this plusarg after BINARY.
        return [str(binary), '--seed=1', '--max-cycles=' + str(cycles), str(elf),
                '+elf_file=' + str(elf)]
    return [str(binary), str(elf), str(cycles)]


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--verilator', type=Path, required=True)
    parser.add_argument('--cpphdl', type=Path, required=True)
    parser.add_argument('--native-graph', action='store_true',
                        help='require the candidate to verify actual graph commits against reported cycles')
    parser.add_argument('--check-commits', action='store_true',
                        help='compare integer architectural commits during validation, not timed trials')
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
    report = dict(equivalent=False, native_graph=args.native_graph, check_commits=args.check_commits, elf=str(elf),
                  elf_sha256=hashlib.sha256(elf.read_bytes()).hexdigest(),
                  binaries={name: dict(path=str(path), sha256=hashlib.sha256(path.read_bytes()).hexdigest())
                            for name, path in binaries.items()}, runs=[])

    def save():
        (output / 'comparison.json').write_text(json.dumps(report, indent=2) + '\n')

    def run(variant, cycles, label):
        directory = output / (label + '-' + variant)
        directory.mkdir(exist_ok=True)
        command = ([] if args.cpu is None else ['taskset', '-c', str(args.cpu)])
        command += simulation_command(binaries[variant], variant, elf, cycles)
        started = time.perf_counter()
        run_environment = dict(environment)
        trace_commits = args.check_commits and label == 'validation' and variant == 'cpphdl'
        if trace_commits:
            run_environment['CPPHDL_TRACE_COMMITS'] = '1'
        try:
            process = subprocess.run(command, cwd=directory, env=run_environment,
                                     stdout=subprocess.PIPE, stderr=subprocess.STDOUT,
                                     timeout=args.timeout)
            text = process.stdout.decode(errors='replace')
            result = parse_run(text, process.returncode,
                               require_graph_edges=args.native_graph and variant == 'cpphdl')
        except subprocess.TimeoutExpired as error:
            text = (error.stdout or b'').decode(errors='replace')
            result = dict(status='host-timeout', passed=False, reason='host timeout')
        (directory / 'run.log').write_text(text)
        result.update(variant=variant, label=label, cap=cycles, command=command,
                      trace_commits=trace_commits,
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

    if args.check_commits:
        try:
            reference_trace = (output / 'validation-verilator/trace_rvfi_hart_00.dasm').read_text()
            candidate_trace = (output / 'validation-cpphdl/run.log').read_text()
            commits = compare_records(verilator_records(reference_trace), cpphdl_records(candidate_trace))
        except (OSError, ValueError) as error:
            commits = dict(equivalent=False, reason=str(error))
        report['integer_commit_comparison'] = commits
        if not commits['equivalent']:
            report['reason'] = 'integer architectural commits differ; no speed ratio'
            save()
            return 1
        save()

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
