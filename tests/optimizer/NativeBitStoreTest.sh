#!/usr/bin/env bash
set -euo pipefail

source "$(dirname "${BASH_SOURCE[0]}")/Search.sh"

cpphdl="$1"
include_dir="$2"
source_dir="$3"
link_options=()
[[ -n "${4:-}" ]] && link_options+=("$4")
build_dir="$(mktemp -d)"
trap 'rm -rf "$build_dir"' EXIT

for mode in --optimize-combs --optimize-combs-l1; do
    "$cpphdl" "$mode" NativeBitStoreRoot --generated-dir="$build_dir" \
        "$source_dir/NativeBitStoreSeed.cc" -- -w -I"$source_dir" -I"$include_dir"
    # Demand real lowering, not a passing comparison of two untouched models.
    search_q 'sv_assign_bit\(n0.result_comb,' "$build_dir"/*.cpp
    search_q 'sv_assign_bit\(n0.wide_comb,' "$build_dir"/*.cpp
    search_q '__cpphdl_bit_target = n0.decoder_comb\[' "$build_dir"/*.cpp
    search_q '__cpphdl_bit_target = n0.packed_wide_comb\[' "$build_dir"/*.cpp
    search_q '__cpphdl_bit_target = n0.nested_comb\[' "$build_dir"/*.cpp
    search_q '__cpphdl_bit_target = n0.unpacked_comb\[' "$build_dir"/*.cpp
    search_q '"; n0.result_comb\[1\] = 1;"' "$build_dir"/*.cpp
    for optimization in -O0 -O2; do
        g++ -std=c++23 "$optimization" -g0 -w -I"$build_dir" -I"$source_dir" \
            -I"$include_dir" "$source_dir/NativeBitStoreRun.cc" \
            "$build_dir"/*.cpp "${link_options[@]}" -o "$build_dir/run"
        "$build_dir/run"
    done
done
