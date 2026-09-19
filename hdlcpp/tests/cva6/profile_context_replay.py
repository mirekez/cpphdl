"""Compare checked BusRun binaries without rebuilding or timing compilation."""

import argparse
from bisect import bisect_right
from collections import Counter, defaultdict
import hashlib
import json
import os
from pathlib import Path
import re
import statistics
import struct
import subprocess


RESULT = re.compile(r"seconds=([\d.]+) cycles=(\d+) checked=(\d+) checksum=([0-9a-f]{16})")


def file_hash(path):
    with Path(path).open("rb") as stream:
        return hashlib.file_digest(stream, "sha256").hexdigest()


def parse_result(output, cycles, checked):
    matches = RESULT.findall(output)
    if len(matches) != 1:
        raise ValueError("expected exactly one successful BusRun result")
    seconds, actual_cycles, actual_checked, checksum = matches[0]
    if int(actual_cycles) != cycles or int(actual_checked) != checked or float(seconds) <= 0:
        raise ValueError("incomplete replay or invalid work time")
    return dict(seconds=float(seconds), cycles=cycles, checked=checked, checksum=checksum)


def category(name):
    # These are exclusive symbol buckets, not claims about inlined operations.
    if "optimized_comb_eval_" in name:
        return "cpphdl demand evaluators (including inlined logic)"
    if "optimized_combs_work_chunk" in name:
        return "cpphdl scheduled work (including inlined logic)"
    if "optimized_combs_strobe" in name or "commit_optimized_regs" in name:
        return "cpphdl register commit"
    if "VXbarBench___024root" in name:
        return "Verilator generated model"
    if "cpphdl::" in name or "::pack(" in name or "::operator=<" in name:
        return "out-of-line datatype/conversion helpers"
    if name == "main" or name.startswith("Bench::step"):
        return "driver (including inlined model work)"
    return "other / unresolved"


def demangle(names):
    names = list(names)
    if not names:
        return {}
    result = subprocess.check_output(["c++filt"], input="\n".join(names) + "\n", text=True)
    return dict(zip(names, result.splitlines()))


def symbols(path):
    result = []
    for flags in ([], ["-D"]):
        command = ["nm", *flags, "-n", "-S", "--defined-only", str(path)]
        output = subprocess.run(command, capture_output=True, text=True, check=False)
        for line in output.stdout.splitlines():
            parts = line.split(maxsplit=3)
            if len(parts) == 4 and parts[2].lower() in ("t", "w", "i"):
                result.append((int(parts[0], 16), int(parts[1], 16), parts[3]))
        if result:
            break
    return sorted(set(result))


def sample_report(prefix):
    modules = []
    for line in prefix.with_suffix(".modules").read_text().splitlines():
        base, start, end, path = line.split(maxsplit=3)
        table = symbols(path) if Path(path).is_file() else []
        modules.append((int(base, 16), int(start, 16), int(end, 16), path,
                        table, [row[0] for row in table]))
    counts = Counter()
    addresses = Counter(address for (address,) in struct.iter_unpack("=Q", prefix.with_suffix(".pcs").read_bytes()))
    for address, count in addresses.items():
        name = "[unmapped]"
        for base, start, end, path, table, starts in modules:
            if start <= address < end:
                offset = address - base
                index = bisect_right(starts, offset) - 1
                name = path + ": [unresolved]"
                if index >= 0 and offset < table[index][0] + table[index][1]:
                    name = table[index][2]
                break
        counts[name] += count
    readable = demangle(counts)
    metadata = json.loads(prefix.with_suffix(".json").read_text())
    if sum(counts.values()) != metadata["samples"] or not counts or metadata["dropped"]:
        raise ValueError("incomplete sample capture")
    rows = [dict(name=readable[name], samples=count, category=category(readable[name]))
            for name, count in counts.most_common()]
    return dict(**metadata, functions=rows)


def callgrind_report(path):
    functions = {}
    costs = defaultdict(Counter)
    calls = Counter()
    edges = Counter()
    events = []
    positions = 1
    current = None
    callee = None
    pending_call = False
    totals = None
    summary = None
    for line in path.read_text().splitlines():
        if line.startswith("events:"):
            events = line.split()[1:]
        elif line.startswith("positions:"):
            positions = len(line.split()) - 1
        elif line.startswith("totals:"):
            totals = dict(zip(events, map(int, line.split()[1:])))
        elif line.startswith("summary:"):
            summary = dict(zip(events, map(int, line.split()[1:])))
        elif line.startswith(("fn=", "cfn=")):
            match = re.fullmatch(r"(c?fn)=\((\d+)\)(?: (.*))?", line)
            if not match:
                raise ValueError("expected compressed Callgrind function names")
            kind, identifier, name = match.groups()
            if name is not None:
                functions[identifier] = name
            if kind == "fn":
                current = identifier
            else:
                callee = identifier
        elif line.startswith("calls="):
            count = int(line[6:].split()[0])
            calls[callee] += count
            edges[current, callee] += count
            pending_call = True
        elif line and (line[0].isdigit() or line[0] in "+-*"):
            if pending_call:
                pending_call = False
            else:
                values = map(int, line.split()[positions:])
                costs[current].update(dict(zip(events, values)))
    sums = Counter()
    for counts in costs.values():
        sums.update(counts)
    if not totals or any(sums[event] != totals.get(event, 0) for event in events):
        raise ValueError("Callgrind exclusive costs do not match totals")
    summary_delta = {event: summary.get(event, 0) - sums[event] for event in events} if summary else {}
    # Cache simulation can charge the final client-request block globally before
    # assigning it to a function. Preserve this tiny boundary difference, but
    # reject cleared/underflowed automatic summaries and larger discrepancies.
    if any(not 0 <= delta <= 64 for delta in summary_delta.values()):
        raise ValueError("Callgrind summary disagrees with exclusive costs; use the gated .1 dump")
    readable = demangle(functions.values())
    rows = [dict(name=readable[functions[identifier]], calls=calls[identifier],
                 category=category(readable[functions[identifier]]), **counts)
            for identifier, counts in costs.items()]
    rows.sort(key=lambda row: row.get("Ir", 0), reverse=True)
    call_rows = [dict(caller=readable[functions[caller]], callee=readable[functions[callee]], calls=count)
                 for (caller, callee), count in edges.most_common()]
    return dict(events=events, totals=dict(sums), summary_delta=summary_delta, functions=rows, edges=call_rows)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--cpphdl", type=Path, required=True)
    parser.add_argument("--verilator", type=Path, required=True)
    parser.add_argument("--trace", type=Path, required=True)
    parser.add_argument("--output", type=Path, required=True)
    parser.add_argument("--cpu", type=int, default=2)
    parser.add_argument("--trials", type=int, default=5)
    parser.add_argument("--sample-repeats", type=int, default=20)
    parser.add_argument("--intervals-us", type=int, nargs="+", default=[1000, 7000])
    parser.add_argument("--valgrind", type=Path)
    parser.add_argument("--valgrind-include", type=Path)
    parser.add_argument("--valgrind-lib", type=Path)
    args = parser.parse_args()
    if args.trials < 1 or args.sample_repeats < 1 or any(not 1000 <= interval <= 999999 for interval in args.intervals_us):
        parser.error("positive repeat/trial counts and sampling intervals >= 1000 us required")
    args.output = args.output.resolve()
    repository = Path(__file__).resolve().parents[3]
    if args.output.is_relative_to(repository):
        parser.error("profile artifacts must be outside the cpphdl repository")
    args.output.mkdir(parents=True, exist_ok=False)
    trace = args.trace.resolve()
    size = trace.stat().st_size
    if not size or size % 800:
        parser.error("expected a nonempty 800-byte-record bus trace")
    checked = size // 800
    binaries = dict(verilator=args.verilator.resolve(), cpphdl=args.cpphdl.resolve())
    main_ranges = {}
    for model, binary in binaries.items():
        matches = [(start, start + size) for start, size, name in symbols(binary) if name == "main"]
        if len(matches) != 1:
            parser.error("unstripped main symbol required: " + str(binary))
        main_ranges[model] = matches[0]
    gate = Path(__file__).with_name("context_replay") / "WorkProfile.c"
    inputs = {str(path): file_hash(path) for path in [trace, gate, Path(__file__), *binaries.values()]}
    environment = {key: value for key, value in os.environ.items()
                   if key != "LD_PRELOAD" and not key.startswith("WORK_PROFILE_")}
    commands = []

    def save(name, value):
        (args.output / name).write_text(json.dumps(value, indent=2) + "\n")

    def run(command, name, extra=None):
        print(name, flush=True)
        commands.append(dict(command=list(map(str, command)), environment=extra or {}))
        save("commands.json", commands)
        with (args.output / (name + ".log")).open("w") as log:
            subprocess.run(list(map(str, command)), env=environment | (extra or {}),
                           stdout=log, stderr=subprocess.STDOUT, check=True)
        return (args.output / (name + ".log")).read_text()

    def execute(model, repeats, name, prefix=None, mode="sample", interval=1000):
        command = ["taskset", "-c", args.cpu]
        extra = {}
        if prefix:
            extra = dict(LD_PRELOAD=str(library), WORK_PROFILE_BINARY=str(binaries[model]),
                         WORK_PROFILE_OUTPUT=str(prefix), WORK_PROFILE_MODE=mode,
                         WORK_PROFILE_MAIN_START=hex(main_ranges[model][0]),
                         WORK_PROFILE_MAIN_END=hex(main_ranges[model][1]),
                         WORK_PROFILE_INTERVAL_US=str(interval))
        if mode == "callgrind":
            if args.valgrind_lib:
                extra["VALGRIND_LIB"] = str(args.valgrind_lib.resolve())
            command += [args.valgrind.resolve(), "--tool=callgrind", "--instr-atstart=no",
                        "--cache-sim=yes", "--branch-sim=yes",
                        "--callgrind-out-file=" + str(prefix.with_suffix(".callgrind"))]
        command += [binaries[model], trace, repeats]
        return parse_result(run(command, name, extra), repeats * checked, checked)

    save("inputs.json", inputs)
    save("environment.json", dict(uname=list(os.uname()), affinity=sorted(os.sched_getaffinity(0)),
                                   cpu=args.cpu, cpuinfo=Path("/proc/cpuinfo").read_text(),
                                   perf_event_paranoid=Path("/proc/sys/kernel/perf_event_paranoid").read_text()))
    library = args.output / "work-profile.so"
    command = ["gcc", "-O2", "-Wall", "-Wextra", "-Werror", "-shared", "-fPIC", gate, "-ldl", "-o", library]
    if args.valgrind:
        command += ["-DUSE_CALLGRIND"]
        if args.valgrind_include:
            command += ["-I" + str(args.valgrind_include.resolve())]
    run(command, "compile-profiler")
    results = dict(timings=[], samples=[], callgrind=[])
    checksums = {}

    def validate(result, repeats):
        expected = checksums.setdefault(repeats, result["checksum"])
        if result["checksum"] != expected:
            raise ValueError("different outputs: performance comparison rejected")

    for trial in range(args.trials):
        for model in list(binaries)[::1 if trial % 2 == 0 else -1]:
            result = execute(model, 1, f"timing-{trial}-{model}")
            validate(result, 1)
            results["timings"].append(dict(model=model, trial=trial, **result))
            save("results.json", results)
    for trial, interval in enumerate(args.intervals_us):
        for model in list(binaries)[::1 if trial % 2 == 0 else -1]:
            name = f"sample-{trial}-{model}"
            prefix = args.output / name
            result = execute(model, args.sample_repeats, name, prefix, interval=interval)
            validate(result, args.sample_repeats)
            profile = sample_report(prefix)
            save(name + "-symbols.json", profile)
            results["samples"].append(dict(model=model, interval_us=interval, **result, profile=profile))
            save("results.json", results)
    if args.valgrind:
        for model in binaries:
            name = "callgrind-" + model
            prefix = args.output / name
            result = execute(model, 1, name, prefix, mode="callgrind")
            validate(result, 1)
            metadata = json.loads(prefix.with_suffix(".json").read_text())
            if metadata["monotonic_calls"] != 2:
                raise ValueError("invalid Callgrind gate")
            profile = callgrind_report(prefix.with_suffix(".callgrind.1"))
            save(name + "-symbols.json", profile)
            results["callgrind"].append(dict(model=model, **result, profile=profile))
            save("results.json", results)
    summary = {}
    for model in binaries:
        times = [row["seconds"] for row in results["timings"] if row["model"] == model]
        median = statistics.median(times)
        counts = Counter()
        for row in results["samples"]:
            if row["model"] == model:
                for function in row["profile"]["functions"]:
                    counts[function["category"]] += function["samples"]
        total = sum(counts.values())
        summary[model] = dict(median_seconds=median, min_seconds=min(times), max_seconds=max(times),
                              ns_per_cycle=median * 1e9 / checked, samples=total,
                              categories=[dict(name=name, samples=count, percent=100 * count / total,
                                               estimated_ns_per_cycle=median * 1e9 / checked * count / total)
                                          for name, count in counts.most_common()])
        for row in results["callgrind"]:
            if row["model"] == model:
                summary[model]["callgrind_per_cycle"] = {
                    event: count / checked for event, count in row["profile"]["totals"].items()}
    for path, digest in inputs.items():
        if file_hash(path) != digest:
            raise RuntimeError("input changed during profiling: " + path)
    save("summary.json", summary)
    print(json.dumps(summary, indent=2))


if __name__ == "__main__":
    main()
