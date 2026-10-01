#!/usr/bin/env bash
set -euo pipefail
cpphdl=$1
include_dir=$2
source_dir=$3
cxx=${4:-g++}
work=$(mktemp -d)
trap 'rm -rf "$work"' EXIT
for mode in --optimize-combs --optimize-combs-l1; do
    CPPHDL_TRACE_COMB_PROOF=1 "$cpphdl" "$mode" IndexedProofRoot --generated-dir="$work" \
        "$source_dir/IndexedProofSeed.cc" -- -std=c++23 -w -I"$include_dir" > "$work/generate.log" 2>&1
    grep -q 'unstable IndexedProofRoot.changing_comb_func' "$work/generate.log"
    ! grep -q 'unstable IndexedProofRoot.stable_comb_func' "$work/generate.log"
    ! grep -q 'unstable IndexedProofRoot.child.result_comb_func' "$work/generate.log"
    for optimization in -O0 -O2; do
        "$cxx" -std=c++23 "$optimization" -g0 -w -I"$work" -I"$source_dir" -I"$include_dir" \
            "$source_dir/IndexedProofRun.cc" "$work"/*.cpp -o "$work/run"
        "$work/run"
    done
done
