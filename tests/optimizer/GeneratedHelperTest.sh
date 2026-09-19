#!/usr/bin/env bash
set -euo pipefail
cpphdl=$1
include_dir=$2
source_dir=$3
cxx=$4
work=$(mktemp -d)
trap 'rm -rf "$work"' EXIT
for mode in direct collection; do
    output="$work/$mode"
    options=()
    input="$source_dir/GeneratedHelperRoot.h"
    if [[ "$mode" == collection ]]; then
        "$cpphdl" --optimize-combs-l1 GeneratedHelperRoot \
            --optimize-combs-collect="$work/helpers.collection" \
            "$source_dir/GeneratedHelperRoot.h" -- -std=c++23 -w -I"$include_dir"
        options+=(--optimize-combs-load="$work/helpers.collection")
        input="$source_dir/GeneratedHelperSeed.cc"
    fi
    "$cpphdl" --optimize-combs-l1 GeneratedHelperRoot "${options[@]}" --generated-dir="$output" \
        "$input" -- -std=c++23 -w -I"$include_dir"
    if grep -Eq '\.__hdlcpp_expr_[0-9]+\(' "$output"/*.cpp; then
        echo 'generated helper still hides producer reads' >&2
        exit 1
    fi
    for optimization in -O0 -O2; do
        "$cxx" -std=c++23 "$optimization" -g0 -w -I"$output" -I"$source_dir" -I"$include_dir" \
            "$source_dir/GeneratedHelperRun.cc" "$output"/*.cpp -o "$work/run"
        "$work/run"
    done
done
