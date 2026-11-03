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
work=$(mktemp -d /tmp/hdlcpp-wildcard-cases.XXXXXX)
cleanup() {
    local status=$? log
    if ((status)); then
        for log in build.log native.log combs.log; do
            [[ ! -f "$work/$log" ]] || tail -n 20 "$work/$log" >&2
        done
    fi
    rm -r -- "$work"
    exit "$status"
}
trap cleanup EXIT
cd "$work"
"$hdlcpp" "$source_dir/wildcard_cases.sv"
"$cxx" -std=c++23 -O0 -g0 -I"$include_dir" -I. "$source_dir/wildcard_cases.cpp" -o run
./run > converted.log
if [[ -n "$verilator" ]]; then
    "$verilator" --cc --exe --build -j 1 --top-module wildcard_cases -Wno-fatal \
        -CFLAGS '-DUSE_VERILATOR -UNDEBUG' --Mdir "$work/reference" \
        "$source_dir/wildcard_cases.sv" "$source_dir/wildcard_cases.cpp" > build.log 2>&1
    "$work/reference/Vwildcard_cases" > reference.log
    cmp converted.log reference.log
fi
if [[ -n "$cpphdl" ]]; then
    printf '#include "generated/wildcard_cases.h"\nextern wildcard_cases elaboration_root;\n' > top.cpp
    "$cpphdl" --native-graph --top elaboration_root --cxx "$cxx" \
        --frontend-flag=-DSYNTHESIS --output "$work/graph" "$work/top.cpp" > native.log 2>&1
    "$cxx" -std=c++23 -O2 -DUSE_NATIVE -I"$work/graph" "$source_dir/wildcard_cases.cpp" -o native
    ./native > native-results.log
    cmp converted.log native-results.log
    "$cpphdl" --optimize-combs wildcard_cases --generated-dir="$work/combs" "$work/top.cpp" \
        -- -std=c++23 -w -I"$include_dir" -I"$work" > combs.log 2>&1
    "$cxx" -std=c++23 -O2 -g0 -fno-access-control -DUSE_COMBS -I"$include_dir" -I. -I"$work/combs" \
        "$source_dir/wildcard_cases.cpp" "$work/combs/"*.cpp -o combs-run
    ./combs-run > combs-results.log
    cmp converted.log combs-results.log
fi
echo 'Wildcard case decoding PASS: 65536 samples'
