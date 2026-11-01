#!/usr/bin/env bash
# Compare one generated graph executable across bounded spin settings.
set -euo pipefail
root=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/../.." && pwd)
work=${1:?usage: BenchmarkNativeSpin.sh NEW_OUTPUT_DIR [WORKERS=6] [TASKS=24]}
workers=${2:-6}
tasks=${3:-24}
[[ $workers =~ ^[0-9]+$ && $workers -ge 1 && $workers -le 256 ]]
[[ $tasks =~ ^[0-9]+$ && $tasks -ge $workers && $tasks -le 256 ]]
for tool in "${CXX:-g++}" jq timeout; do command -v "$tool" >/dev/null; done
mkdir -- "$work"
work=$(cd -- "$work" && pwd)
sha256sum "$root/include/cpphdl_graph_threads.h" > "$work/runtime.sha256"
lscpu > "$work/cpu.txt"
taskset -pc $$ >> "$work/cpu.txt"
echo 'Building native graph fixture'
"${CXX:-g++}" -std=c++23 -O1 -pthread -I"$root/include" \
    "$root/tests/graph/NativeLocalTasksEmit.cc" -o "$work/emit"
"$work/emit" "$work/model.h" "$tasks" 1 "$workers"
"${CXX:-g++}" -std=c++23 -O1 -pthread -I"$root/include" -I"$work" \
    "$root/tests/graph/NativeLocalTasksRun.cc" -o "$work/run"
for spin in 0 64 256 1024 4096; do
    for policy in serial parallel auto; do
        CPPHDL_NATIVE_SPIN=$spin CPPHDL_NATIVE_DISPATCH=$policy timeout 120 "$work/run"
    done
done
printf 'trial\tpolicy\tmode\tspins\tclocks\tseconds\tclocks_per_second\teffective_cpus\n' > "$work/results.tsv"
for trial in 1 2 3; do
    spins=(64 256 1024 4096)
    if (( trial == 2 )); then spins=(4096 1024 256 64); fi
    for policy in parallel auto; do
        for mode in 0 1 2 3; do
            for spin in "${spins[@]}"; do
                log=$work/$trial-$policy-$mode-$spin.json
                CPPHDL_NATIVE_SPIN=$spin CPPHDL_NATIVE_DISPATCH=$policy \
                    timeout 120 "$work/run" "$mode" > "$log"
                printf '%s\t%s\t%s\t%s\t%s\n' "$trial" "$policy" "$mode" "$spin" \
                    "$(jq -r '[.clocks,.seconds,(.clocks/.seconds),(.cpu_seconds/.seconds)] | @tsv' "$log")" \
                    | tee -a "$work/results.tsv"
            done
        done
    done
done
echo "PASS graph oracle checks; throughput measurements: $work/results.tsv"
