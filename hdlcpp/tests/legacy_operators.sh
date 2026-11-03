#!/usr/bin/env bash
set -euo pipefail
ulimit -c 0
hdlcpp=$(realpath "$1")
include_dir=$(realpath "$2")
cxx=$(command -v "$3")
cxx=$(cd "$(dirname "$cxx")" && pwd)/$(basename "$cxx")
verilator=${4:-}
cpphdl=${5:-}
[[ -z "$cpphdl" ]] || cpphdl=$(realpath "$cpphdl")
source_dir=$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)
work=$(mktemp -d /tmp/hdlcpp-legacy-operators.XXXXXX)
cleanup() {
    status=$?
    if (( status != 0 )); then
        for log in "$work/build.log" "$work/native.log" "$work/graph/cpp-to-graph.log"; do
            [[ ! -f "$log" ]] || tail -n 30 "$log" >&2
        done
    fi
    rm -r -- "$work"
    exit "$status"
}
trap cleanup EXIT
cd "$work"
"$hdlcpp" "$source_dir/legacy_operators.sv"
"$cxx" -std=c++23 -O0 -g0 -I"$include_dir" -I. "$source_dir/legacy_operators.cpp" -o run
./run > converted.log
if [[ -n "$verilator" ]]; then
    "$verilator" --cc --exe --build -j 1 --top-module legacy_operators -Wno-fatal \
        -CFLAGS '-DUSE_VERILATOR -UNDEBUG' --Mdir "$work/reference" \
        "$source_dir/legacy_operators.sv" "$source_dir/legacy_operators.cpp" > build.log 2>&1
    "$work/reference/Vlegacy_operators" > reference.log
    cmp converted.log reference.log
fi
if [[ -n "$cpphdl" ]]; then
    printf '#include "generated/legacy_operators.h"\nextern legacy_operators elaboration_root;\n' > top.cpp
    "$cpphdl" --native-graph --top elaboration_root --cxx "$cxx" \
        --frontend-flag=-DSYNTHESIS --output "$work/graph" "$work/top.cpp" > native.log 2>&1
    "$cxx" -std=c++23 -O2 -DUSE_NATIVE -I"$work/graph" "$source_dir/legacy_operators.cpp" -o native
    ./native > native-results.log
    cmp converted.log native-results.log
fi
echo 'Legacy operators and concatenated register writes PASS'
