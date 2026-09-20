#!/usr/bin/env bash
set -euo pipefail
cpphdl=$1
include_dir=$2
source_dir=$3
link_options=()
[[ -n "${4:-}" ]] && link_options+=("$4")
build_root=$(mktemp -d)
trap 'rm -rf "$build_root"' EXIT
for mode in --optimize-combs --optimize-combs-l1; do
    generated="$build_root/${mode#--}"
    "$cpphdl" "$mode" ProjectedHandshakeRoot --generated-dir="$generated" \
        "$source_dir/ProjectedHandshakeSeed.cc" -- -std=c++23 -w -I"$include_dir" -I"$source_dir"
    for optimization in -O0 -O2; do
        g++ -std=c++23 "$optimization" -g0 -w -I"$generated" -I"$include_dir" -I"$source_dir" \
            "$source_dir/ProjectedHandshakeRun.cc" "$generated"/*.cpp \
            "${link_options[@]}" -o "$generated/run"
        "$generated/run"
    done
done
