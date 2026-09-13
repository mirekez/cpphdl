#!/usr/bin/env bash
set -euo pipefail
source "$(dirname "${BASH_SOURCE[0]}")/Search.sh"
cpphdl="$1"
include_dir="$2"
source_dir="$3"
link_options=()
[[ -n "${4:-}" ]] && link_options+=("$4")
build_root="$(mktemp -d)"
trap 'rm -rf "$build_root"' EXIT
for mode in --optimize-combs --optimize-combs-l1; do
    generated="$build_root/${mode#--}"
    "$cpphdl" "$mode" SpecializedCombRoot --generated-dir="$generated" \
        "$source_dir/SpecializedCombSeed.cc" -- -std=c++23 -w -I"$include_dir" -I"$source_dir"
    # Selected assignments must not regain discarded branches. The scoped
    # getter still needs Width and its destructor must run before the next read.
    if search_q 'if constexpr' "$generated"/*.cpp; then
        echo "specialized comb reverted to its generic pattern" >&2
        exit 1
    fi
    search_q 'selected_comb =' "$generated"/*.cpp
    search_q 'logic<Width>' "$generated"/*.cpp
    search_q 'guard\{temporary_count\}' "$generated"/*.cpp
    for optimization in -O0 -O2; do
        g++ -std=c++23 "$optimization" -g0 -w -I"$generated" -I"$include_dir" -I"$source_dir" \
            "$source_dir/SpecializedCombRun.cc" "$generated"/*.cpp "${link_options[@]}" -o "$generated/run"
        "$generated/run"
    done
done
